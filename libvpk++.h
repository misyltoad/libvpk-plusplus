#pragma once

#include <fstream>
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <algorithm>

namespace libvpk {

  namespace helpers {

    std::string_view removeExtension(std::string_view string, std::string_view ending) {
      const size_t position = string.find(ending);

      return position != std::string_view::npos
        ? string.substr(0, position)
        : string;
    }

    std::string_view normalizePath(std::string_view path) {
      // Remove .vpk and _dir
      path = helpers::removeExtension(path, ".vpk");
      path = helpers::removeExtension(path, "_dir");

      return path;
    }

    template <typename T>
    T read(std::ifstream& stream) {
      T value;
      stream.read(reinterpret_cast<char*>(&value), sizeof(T));
      return value;
    }

    template <>
    std::string read<std::string>(std::ifstream& stream) {
      std::string value;
      std::getline(stream, value, '\0');
      return value;
    }

    std::string directoryPath(std::string_view basePath) {
      return std::string(basePath) + "_dir.vpk";
    }

    std::string archivePath(std::string_view basePath, uint16_t archiveIndex) {
      std::stringstream archivePath;
      archivePath << basePath << "_" << std::setw(3) << std::setfill('0') << archiveIndex << ".vpk";
      return archivePath.str();
    }

    class NonCopyable
    {
      protected:
        NonCopyable() {}
        ~NonCopyable() {}
      private: 
        NonCopyable(const NonCopyable &);
        NonCopyable& operator=(const NonCopyable &);
    };

  }

  namespace meta {

    struct VPKHeader1 {
      static constexpr uint32_t ValidSignature = 0x55aa1234;

      int32_t signature = 0;
      int32_t version   = 0;
      int32_t treeSize  = 0;
    };

    struct VPKHeader2 : public VPKHeader1 {
      VPKHeader2() = default;
      VPKHeader2(const VPKHeader1& header) : VPKHeader1(header) { }

      int32_t fileDataSectionSize   = 0;
      int32_t archiveMD5SectionSize = 0;
      int32_t otherMD5SectionSize   = 0;
      int32_t signatureSectionSize  = 0;
    };

    using VPKHeader = VPKHeader2;

  }

  class VPKArchive : private helpers::NonCopyable {

  public:

    std::string_view directoryPath() {
      return m_directoryPath;
    }

    std::string_view archivePath() {
      return m_archivePath;
    }

  protected:

    VPKArchive(std::string_view basePath, uint16_t archiveIndex)
      : m_directoryPath(helpers::directoryPath(basePath))
      , m_archivePath(helpers::archivePath(basePath, archiveIndex)) {
      if (!std::filesystem::exists(m_archivePath))
        throw std::exception("VPK archive doesn't exist");
    }

    friend class VPKSet;

  private:

    std::string m_directoryPath;
    std::string m_archivePath;

  };

  using VPKArchiveRef = std::shared_ptr<VPKArchive>;

  class VPKFile {

  public:

    VPKFile() = default;
    VPKFile(const VPKFile&) = default;
    VPKFile& operator=(const VPKFile&) = default;

    const VPKArchiveRef archive() const {
      return m_archive;
    }

    int32_t crc() const {
      return m_desc.crc;
    }

    int32_t length() const {
      return m_desc.preloadLength + m_desc.fileLength;
    }

  protected:

    struct VPKFileDesc {
      int32_t preloadOffset;
      int32_t preloadLength;

      int32_t fileOffset;
      int32_t fileLength;

      int32_t crc;
    };

    VPKFile(VPKArchiveRef archive, VPKFileDesc desc)
      : m_archive(archive), m_desc(desc) { }

    friend class VPKSet;
    friend class VPKFileStream;

  private:

    VPKFileDesc   m_desc;

    VPKArchiveRef m_archive;

  };

  class VPKFileStream {

  public:

    VPKFileStream(const VPKFile& file)
      : VPKFileStream(file.m_archive, file.m_desc) { }

    int32_t read(char* dst, int32_t count) {
      int32_t preloadCount = std::min(pos + count, m_preloadLength) - pos;
      int32_t fileCount    = std::min(pos + count - m_preloadLength, m_fileLength) - pos;

      preloadCount = std::max(preloadCount, 0);
      fileCount    = std::max(fileCount, 0);

      if (preloadCount != 0)
        m_preloadStream.read(dst, preloadCount);

      if (fileCount != 0)
        m_archiveStream.read(dst, fileCount);

      count = preloadCount + fileCount;
      pos += count;

      return count;
    }

    int32_t seek(int32_t pos) {
      int32_t oldPreloadPos = preloadPos();
      int32_t oldFilePos    = filePos();

      pos = std::clamp(pos, 0, m_preloadLength + m_fileLength);

      int32_t preloadOff = preloadPos() - oldPreloadPos;
      int32_t fileOff    = filePos()    - oldFilePos;

      m_preloadStream.seekg(preloadOff, std::ios::cur);
      m_archiveStream.seekg(fileOff,    std::ios::cur);

      return pos;
    }

  protected:

    VPKFileStream(VPKArchiveRef archive, VPKFile::VPKFileDesc desc)
      : m_preloadStream(archive->directoryPath(), std::ios::binary)
      , m_archiveStream(archive->archivePath(),   std::ios::binary)
      , m_preloadLength(desc.preloadLength)
      , m_fileLength   (desc.fileLength) {
      m_preloadStream.seekg(desc.preloadOffset);
      m_archiveStream.seekg(desc.fileOffset);
    }

  private:

    int32_t preloadPos() {
      return std::min(pos, m_preloadLength);
    }

    int32_t filePos() {
      return std::clamp(pos - m_preloadLength, 0, m_fileLength);
    }

    std::ifstream m_preloadStream;
    std::ifstream m_archiveStream;

    int32_t m_preloadLength;
    int32_t m_fileLength;

    int32_t pos = 0;
  };

  using VPKFileMap = std::unordered_map<std::string, VPKFile>;

  class VPKSet : private helpers::NonCopyable {

  public:

    VPKSet(std::string_view path) {
      std::string_view basePath = helpers::normalizePath(path);

      // Load the VPK directory file
      std::string directoryPath = std::string(basePath) + "_dir.vpk";

      auto directoryStream = std::ifstream(directoryPath, std::ios::binary);
      if (!directoryStream.is_open() || directoryStream.bad())
        throw std::exception("Couldn't find/open VPK directory.");

      auto initialHeader = helpers::read<meta::VPKHeader1>(directoryStream);
      if (initialHeader.signature != meta::VPKHeader1::ValidSignature)
        throw std::exception("Invalid VPK directory signature.");

      if (initialHeader.version == 1)
        m_header = initialHeader;
      else if (initialHeader.version == 2) {
        // Return to the beginning and read a full VPK 2 ptr.
        directoryStream.seekg(0);
        m_header = helpers::read<meta::VPKHeader2>(directoryStream);
      }
      else
        throw std::exception("Unknown VPK version.");

      parseDirectory(basePath, std::move(directoryStream));
    }

    meta::VPKHeader header() {
      return m_header;
    }

    std::optional<VPKFile> file(const std::string& path) {
      auto iter = m_files.find(path);
      if (iter == m_files.end())
        return std::nullopt;

      return iter->second;
    }

    const VPKFileMap& files() {
      return m_files;
    }

  private:

    void parseDirectory(std::string_view basePath, std::ifstream&& stream) {
      for (;;) {
        auto extension = helpers::read<std::string>(stream);
        if (extension.empty())
          break;

        for (;;) {
          auto path = helpers::read<std::string>(stream);
          if (path.empty())
            break;

          for (;;) {
            auto name = helpers::read<std::string>(stream);
            if (name.empty())
              break;

            std::string fullPath = path + '/' + name + '.' + extension;

            parseFile(basePath, stream, fullPath);
          }
        }
      }
    }

    void parseFile(std::string_view basePath, std::ifstream& stream, const std::string& vpkFilePath) {
      int32_t crc          = helpers::read<int32_t>(stream);
      int16_t preloadBytes = helpers::read<int16_t>(stream);
      int16_t archiveIndex = helpers::read<int16_t>(stream);

      int32_t offset = helpers::read<int32_t>(stream);
      int32_t length = helpers::read<int32_t>(stream);
      
      // Read the terminator for the file info.
      helpers::read<int16_t>(stream);

      // Resize our archive reference vector if
      // we need more space.
      if (archiveIndex >= int16_t(m_archives.size()))
        m_archives.resize(archiveIndex + 1);

      // Check if we need to load this archive
      if (m_archives[archiveIndex] == nullptr)
        m_archives[archiveIndex] = std::shared_ptr<VPKArchive>(new VPKArchive(basePath, archiveIndex));

      VPKFile::VPKFileDesc desc;
      desc.preloadOffset = int32_t(stream.tellg());
      desc.preloadLength = preloadBytes;
      desc.fileOffset    = offset;
      desc.fileLength    = length;
      desc.crc           = crc;

      // Skip over the preload section
      if (desc.preloadLength != 0)
        stream.ignore(desc.preloadLength);

      m_files.emplace(vpkFilePath, VPKFile(m_archives[archiveIndex], desc));
    }

    meta::VPKHeader m_header;

    std::vector<std::shared_ptr<VPKArchive>> m_archives;

    VPKFileMap m_files;

  };

}