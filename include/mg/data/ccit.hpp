#pragma once

#include <cstdint>
#include <string>

namespace mg::data {

struct GlyphInfo {
  std::string char_;
  uint32_t index;
  uint32_t page;
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;

  GlyphInfo(const std::string &c, uint32_t idx, uint32_t p = 0, uint32_t x_ = 0,
            uint32_t y_ = 0, uint32_t w_ = 0, uint32_t h_ = 0)
    : char_(c), index(idx), page(p), x(x_), y(y_), w(w_), h(h_) {}
};

// Convert text table to ccit binary format
bool txt_to_ccit(const std::string &src, std::string &dest);

// Convert ccit binary data to text table
bool ccit_to_txt(const std::string &src, std::string &dest);

} // namespace mg::data
