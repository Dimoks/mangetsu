#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <png.h>
#include <mg/util/endian.hpp>
#include <mg/data/mzp.hpp>
#include <mg/data/mzx.hpp>

namespace mg::data {

class MzpImage {
public:
  struct __attribute__((packed)) MzpImageHeader {
    uint16_t width;
    uint16_t height;
    uint16_t tile_width;
    uint16_t tile_height;
    uint16_t tile_x_count;
    uint16_t tile_y_count;
    uint16_t bmp_type;
    uint8_t bmp_depth;
    uint8_t tile_crop;
  };

  struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;

    Color() : r(0), g(0), b(0), a(255) {}

    Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
          : r(red), g(green), b(blue), a(alpha) {}
  };

  const MzpImageHeader &header;
  MzpImage() = delete;
  MzpImage(const std::string &data);
  bool is_valid() const { return is_valid_; }

  bool write_mzp(std::string &output);
  bool img_read(const std::string &png_data, int compression_level,
                bool overlap = false);
  bool img_write(std::string &output, bool overlap = false);
                  // const std::string &name = "");

  bool getTileData(int index, std::string &out_data) const {
    if (index >= static_cast<int>(mzp_archive.entry_data.size())) {
      fprintf(stderr, "Tile index %d out of range.\n", index);
      return false;
    }
    out_data = mzp_archive.entry_data[index];
    return true;
  }

  void addTile(const std::string &tile) {
    tiles.push_back(tile);
  }

private:
  Mzp mzp_archive;
  MzpImageHeader header_;
  bool is_valid_ = false;
  int channels;
  int bits_per_px;
  size_t tile_size;
  std::vector<std::string> tiles;
  std::vector<Color> palette;

  bool read_image_header(const std::string &data);
  // bool write_image_header(std::string &out) const;

  int get_bits_per_px() const;
  int nb_channels() const;
  std::vector<Color> get_palette();
  void set_palette();
  int get_filler_index();

  size_t get_tile_size() const;

  std::vector<uint8_t> get_tile(size_t index) const;
  void set_tile(int index, const std::vector<uint8_t> &pixels,
                                        int compression_level);
};

std::vector<uint8_t> rgb565_unpack(const std::vector<uint16_t> &pq,
                                    const std::vector<uint8_t> &offsets_byte);
std::pair<std::vector<uint16_t>, std::vector<uint8_t>>
rgb565_pack(const std::vector<uint8_t> &rgb);
} // namespace mg::data
