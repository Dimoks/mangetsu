#include <mg/data/ccit.hpp>

namespace mg::data {

bool has_coords = false;

GlyphInfo line_to_glyph(const std::string &line) {
  std::istringstream iss(line);
  std::string token;
  std::vector<std::string> tokens;

  // Split by double tab
  while (std::getline(iss, token, '\t')) {
    if (!token.empty() && iss.peek() == '\t') {
      iss.get(); // Skip second tab
      tokens.push_back(token);
    }
  }

  if (!token.empty()) {
    tokens.push_back(token);
  }

  if (tokens.size() < 2) {
    fprintf(stderr, "Invalid line format: %s\n", line.c_str());
    return GlyphInfo("", 0);
  }

  // Parse char
  std::string char_str = tokens[0].substr(tokens[0].find('=') + 1);
  // Parse index
  uint32_t index = 0;

  try {
    index = std::stoul(tokens[1].substr(tokens[1].find('=') + 1));
  } catch (const std::exception &e) {
    fprintf(stderr, "Failed to parse index in line: %s\n", line.c_str());
    return GlyphInfo("", 0);
  }

  // Parse coordinates if present
  uint32_t page = 0, x = 0, y = 0, w = 0, h = 0;
  has_coords = tokens.size() > 2;
  if (has_coords) {
    try {
      page = std::stoul(tokens[2].substr(tokens[2].find('=') + 1));
      x = std::stoul(tokens[3].substr(tokens[3].find('=') + 1));
      y = std::stoul(tokens[4].substr(tokens[4].find('=') + 1));
      w = std::stoul(tokens[5].substr(tokens[5].find('=') + 1));
      h = std::stoul(tokens[6].substr(tokens[6].find('=') + 1));
    } catch (const std::exception &e) {
      fprintf(stderr, "Failed to parse coordinates in line: %s\n",
                      line.c_str());
      return GlyphInfo("", 0);
    }
  }

  return GlyphInfo(char_str, index, page, x, y, w, h);
}

// Convert uint32_t to UTF-8 string
static std::string uint32_to_utf8(uint32_t packed_bytes) {
  uint8_t bytes[4] = {0};
  size_t len = 0;

  for (int i = 3; i >= 0; --i) {
    uint8_t byte = (packed_bytes >> (8 * i)) & 0xFF;
    if (byte != 0x00 || len > 0) {
      bytes[len++] = byte;
    }
  }

  if (len == 0) {
    fprintf(stderr, "Empty or invalid character.\n");
    return "?";
  }

  return std::string(reinterpret_cast<const char*>(bytes), len);
}

bool txt_to_ccit(const std::string &src, std::string &dest) {
  std::istringstream iss(src);
  std::string line;
  std::vector<GlyphInfo> glyphs;

  // Parse lines into glyphs
  while (std::getline(iss, line)) {
    if (!line.empty()) {
      glyphs.push_back(line_to_glyph(line));
    }
  }

  if (glyphs.empty()) {
    fprintf(stderr, "No glyphs found in input.\n");
    return "";
  }

  std::string result;
  // Write number of glyphs and index size (8 bytes: char + index)
  uint32_t num_glyphs = host_to_le_u32(static_cast<uint32_t>(glyphs.size()));
  uint32_t index_size = host_to_le_u32(8);
  dest.append(reinterpret_cast<const char*>(&num_glyphs), sizeof(uint32_t));
  dest.append(reinterpret_cast<const char*>(&index_size), sizeof(uint32_t));

  // Write char and index
  for (const auto &glyph : glyphs) {
    // Convert char to UTF-8 bytes and interpret as 32-bit integer in LE
    std::string char_bytes = glyph.char_;
    uint32_t char_int = 0;

    if (char_bytes.size() > 4) {
      fprintf(stderr, "Character too long: %s\n", glyph.char_.c_str());
      char_bytes.resize(4);
    }

    for (size_t i = 0; i < char_bytes.size(); ++i) {
      char_int |= (static_cast<uint8_t>(char_bytes[i]) <<
                  (8 * (char_bytes.size() - 1 - i)));
    }

    char_int = host_to_le_u32(char_int);
    dest.append(reinterpret_cast<const char*>(&char_int), sizeof(uint32_t));
    uint32_t index = host_to_le_u32(glyph.index);
    dest.append(reinterpret_cast<const char*>(&index), sizeof(uint32_t));
  }

  // Write coordinates if present

  uint32_t coords_size = has_coords ? host_to_le_u32(20) : 0;
  uint32_t coords_num = host_to_le_u32(has_coords ? glyphs.size() : 0);
  dest.append(reinterpret_cast<const char*>(&coords_num), sizeof(uint32_t));

  if (has_coords) {
    dest.append(reinterpret_cast<const char*>(&coords_size), sizeof(uint32_t));
    for (const auto &glyph : glyphs) {
      uint32_t page = host_to_le_u32(glyph.page);
      uint32_t x = host_to_le_u32(glyph.x);
      uint32_t y = host_to_le_u32(glyph.y);
      uint32_t w = host_to_le_u32(glyph.w);
      uint32_t h = host_to_le_u32(glyph.h);
      dest.append(reinterpret_cast<const char*>(&page), sizeof(uint32_t));
      dest.append(reinterpret_cast<const char*>(&x), sizeof(uint32_t));
      dest.append(reinterpret_cast<const char*>(&y), sizeof(uint32_t));
      dest.append(reinterpret_cast<const char*>(&w), sizeof(uint32_t));
      dest.append(reinterpret_cast<const char*>(&h), sizeof(uint32_t));
    }
  }

  return true;
}

bool ccit_to_txt(const std::string &src, std::string &dest) {
  if (src.size() < 8) {
    fprintf(stderr, "Invalid ccit data: too short.\n");
    return false;
  }

  // Read number of glyphs and index size
  uint32_t num_glyphs =
      le_to_host_u32(*reinterpret_cast<const uint32_t*>(src.data()));
  uint32_t index_size =
      le_to_host_u32(*reinterpret_cast<const uint32_t*>(src.data() + 4));

  if (src.size() < 8 + num_glyphs * index_size) {
    fprintf(stderr, "Invalid ccit data: insufficient size for glyphs.\n");
    return false;
  }

  // Read glyphs
  std::vector<GlyphInfo> glyphs;
  for (uint32_t i = 0; i < num_glyphs; ++i) {
    const char *ptr = src.data() + 8 + i * index_size;
    uint32_t char_int = le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr));
    // Convert to UTF-8 string
    std::string char_str = uint32_to_utf8(char_int);
    uint32_t index =
        le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr + 4));
    glyphs.emplace_back(char_str, index);
  }

  // Read coordinates if present
  size_t coords_offset = 8 + num_glyphs * index_size;
  if (src.size() >= coords_offset + 4) {
    uint32_t coords_num =
        le_to_host_u32(*reinterpret_cast<const uint32_t*>(src.data() +
                        coords_offset));
    if (coords_num > 0) {
      if (src.size() < coords_offset + 8) {
        fprintf(stderr, "Invalid ccit data: insufficient "
                        "size for coords size.\n");
        return false;
      }

      uint32_t coords_size =
          le_to_host_u32(*reinterpret_cast<const uint32_t*>(src.data() +
                          coords_offset + 4));

      if (src.size() < coords_offset + 8 + coords_num * coords_size) {
        fprintf(stderr, "Invalid ccit data: insufficient "
                        "size for coordinates.\n");
        return false;
      }

      for (uint32_t i = 0; i < coords_num; ++i) {
        const char *ptr = src.data() + coords_offset + 8 + i * coords_size;
        glyphs[i].page =
            le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr));
        glyphs[i].x =
            le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr + 4));
        glyphs[i].y =
            le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr + 8));
        glyphs[i].w =
            le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr + 12));
        glyphs[i].h =
            le_to_host_u32(*reinterpret_cast<const uint32_t*>(ptr + 16));
      }
    }
  }

  // Convert to text
  for (const auto &glyph : glyphs) {
    if (glyph.page != 0 || glyph.x != 0 || glyph.y != 0 ||
        glyph.w != 0 || glyph.h != 0) {

      dest += mg::string::format("char=%s\t\tindex=%u\t\tpage=%u\t\tx=%u\t\t"
                                  "y=%u\t\twidth=%u\t\theight=%u\n",
                                  glyph.char_.c_str(), glyph.index, glyph.page,
                                  glyph.x, glyph.y, glyph.w, glyph.h);
    } else {
      dest += mg::string::format("char=%s\t\tindex=%u\n", glyph.char_.c_str(),
                                  glyph.index);
    }
  }

  return true;
}

} // namespace mg::data
