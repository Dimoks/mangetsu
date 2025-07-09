#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <mg/data/mzpimage.hpp>

namespace mg::data {

// Structure to hold edge flags for a tile
struct EdgeFlags {
  bool left;
  bool right;
  bool top;
  bool bottom;
};

std::pair<std::vector<uint8_t>, std::vector<MzpImage::Color>>
quantize_image_imagequant(const std::vector<uint8_t> &image_data,
                          int width, int height, int max_colors,
                          const EdgeFlags *edge_flags = nullptr);

} // namespace mg::data
