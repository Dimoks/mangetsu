#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mg/data/mzpimage.hpp>
#include <mg/data/quantize.hpp>

namespace mg::data {

uint8_t fix_alpha(uint8_t a);

uint8_t unfix_alpha(uint8_t a);

bool hep_extract_tile(const std::string &data, std::vector<uint8_t> &tile,
                      const MzpImage &mzp);

void hep_insert_tile(MzpImage &mzp, int tile_index,
                     const std::vector<uint8_t> &pixels,
                     int compression_level = 0);

} // namespace mg::data
