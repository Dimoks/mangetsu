// mg/data/lenzu.hpp
#pragma once

#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <string>
#include <mg/util/endian.hpp>
#include <mg/data/bitstream.hpp>
#include <mg/data/huffman.hpp>

namespace mg::data {

class Lenzu {
public:
  static constexpr char Lenzu_magic[] = "LenZuCompressor";
  static constexpr char version_info[] = "\x31\0\0\0\x30\0\0\0\0\0\0\0\0\0\0";
  static bool decompress(const std::string &src,
                          std::string &dest, bool invert);
private:

  struct __attribute__((packed)) Header {
    uint8_t magic[sizeof(Lenzu_magic)];
    uint8_t version[sizeof(version_info)];
    uint32_t decompressed_length;
    uint64_t crc;
    uint32_t reserved;
    uint8_t options[6];

    void to_host_order();
  };

  static ByteHuffmanTable read_huffman_table(const std::string &src,
                                              size_t &pos, uint32_t bit_count,
                                              bool invert);
  static uint64_t lenzu_crc_64(const std::string &data, size_t size,
                                uint64_t seed = 0, size_t lut_offset = 0);
  static uint32_t lenzu_crc_32(const std::string &data, size_t size,
                                uint32_t seed = 0, size_t lut_offset = 0);
};

} // namespace mg::data
