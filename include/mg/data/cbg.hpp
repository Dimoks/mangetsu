// mg/data/cbg.hpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <png.h>
#include <mg/data/huffman.hpp>
#include <mg/data/bitstream.hpp>
#include <mg/util/endian.hpp>

namespace mg::data {

class CompressedBG {
public:
  static constexpr const char CBG_MAGIC[] = "CompressedBG_MT";
  explicit CompressedBG(const std::string &src);

  bool img_write(std::string &output) const;
  bool img_read(const std::string &src);
  bool cbg_write(std::string &dest) const;

  bool is_valid() const { return is_valid_; }

private:
  bool is_valid_ = false;

  struct __attribute__((packed)) Header {
    uint8_t magic[sizeof(CBG_MAGIC)];
    uint32_t width;
    uint32_t height;
    uint32_t stripe_h;
    uint32_t bpp;
    uint8_t padding[16];

    void to_host_order();
  };

  Header header;

  uint32_t nb_stripes;
  std::vector<std::pair<size_t, size_t>> stripes;
  std::vector<uint8_t> _data;

  uint32_t stripe_height(uint32_t index) const;
  bool decompress_stripe(uint32_t index, std::vector<uint8_t> &output) const;
  bool compress_stripe(const uint8_t *pixels, uint32_t index,
                        std::vector<uint8_t> &output) const;

  static uint32_t read_variable(const std::vector<uint8_t> &data,
                                 size_t &pos, size_t max_pos);
  static void write_variable(std::vector<uint8_t> &output, uint32_t value);
};

} // namespace mg::data
