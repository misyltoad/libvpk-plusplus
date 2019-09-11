#include "../libvpk++.h"

#include <iostream>
#include <filesystem>

// This is an example application of using
// libvpk++ to dump a VPK.

// This is not intended to be used as a real
// application and is bottlenecked by fairly
// shoddy design (create_directories)

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cout << "Usage: vpk_dumper <path to vpk>" << std::endl;  
    return 1;
  }

  try {
    auto set = libvpk::VPKSet(std::string(argv[1]));

    std::vector<char> fileBuffer;

    for (const auto& file : set.files()) {
      auto path = std::filesystem::path(file.first);
      std::filesystem::create_directories(path.remove_filename());

      auto inStream = libvpk::VPKFileStream(file.second);
      auto outStream = std::ofstream(file.first, std::ios::binary | std::ios::out);

      if (outStream.bad() || !outStream.is_open())
        std::cout << "Failed to write file: " << file.first << std::endl;

      int32_t length = file.second.length();

      if (length >= int32_t(fileBuffer.size()))
        fileBuffer.resize(length);

      inStream.read(fileBuffer.data(), length);
      outStream.write(fileBuffer.data(), length);
    }
  }
  catch (std::exception e) {
    std::cout << "Fatal error dumping VPK:" << std::endl;
    std::cout << e.what() << std::endl;
    return 1;
  }

  return 0;
}