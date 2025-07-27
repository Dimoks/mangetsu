// src/data/cbg.cpp
#include <mg/data/cbg.hpp>

namespace mg::data {

void CompressedBG::Header::to_host_order() {
  width = le_to_host_u32(width);
  height = le_to_host_u32(height);
  stripe_h = le_to_host_u32(stripe_h);
  bpp = le_to_host_u32(bpp);
}

CompressedBG::CompressedBG(const std::string &src)
    : _data(src.begin(), src.end()) {

  if (src.size() < sizeof(Header)) {
    fprintf(stderr, "Error: Input too short for header, "
                    "got %zu bytes\n", src.size());
    return;
  }

  header = *reinterpret_cast<const Header*>(&src[0]);
  header.to_host_order();

  // Validate magic
  if (std::memcmp(header.magic, CBG_MAGIC, sizeof(CBG_MAGIC)) != 0) {
    fprintf(stderr, "Error: Invalid CBG magic.\n");
    return;
  }

  size_t start = sizeof(Header);

  auto read32 = [&](size_t pos) {
    if (pos + 4 > _data.size()) {
      fprintf(stderr, "Error: Invalid CBG header access at pos %zu\n", pos);
      return uint32_t(0);
    }
    return le_to_host_u32(*reinterpret_cast<const uint32_t*>(
                          _data.data() + pos));
  };

  nb_stripes = (header.height + header.stripe_h - 1) / header.stripe_h;

  if (_data.size() < start + nb_stripes * 4) {
    fprintf(stderr, "Error: Invalid CBG header size.\n");
    return;
  }

  stripes.reserve(nb_stripes);
  for (uint32_t i = 0; i < nb_stripes; ++i) {
    size_t offset = read32(start + i * 4);
    size_t next_offset = (i == nb_stripes - 1) ? _data.size() :
                          read32(start + (i + 1) * 4);
    if (offset > _data.size() || next_offset > _data.size() ||
        offset > next_offset) {
      fprintf(stderr, "Error: Invalid stripe offset %zu or size "
                      "for stripe %u\n", offset, i);
      return;
    }
    stripes.emplace_back(offset, next_offset - offset);
  }

  is_valid_ = true;
}

uint32_t CompressedBG::stripe_height(uint32_t index) const {
  if (index < nb_stripes - 1) return header.stripe_h;
  uint32_t h = header.height % header.stripe_h;
  return h == 0 ? header.stripe_h : h;
}

uint32_t CompressedBG::read_variable(const std::vector<uint8_t> &data,
                                      size_t &pos, size_t max_pos) {
  uint32_t value = 0;
  uint32_t shift = 0;
  uint8_t byte;
  size_t start_pos = pos;

  do {
    if (pos >= max_pos) {
      fprintf(stderr, "Error: Invalid variable integer at pos %zu "
                      "(started at %zu)\n", pos, start_pos);
      return 0;
    }
    byte = data[pos++];
    value |= (byte & 0x7F) << shift;
    shift += 7;
    if (shift > 32) {
      fprintf(stderr, "Error: Variable integer overflow at pos %zu\n", pos);
      return 0;
    }
  } while (byte & 0x80);

  return value;
}

void CompressedBG::write_variable(std::vector<uint8_t> &output,
                                    uint32_t value) {
  while (value > 0x7F) {
    output.push_back((value & 0x7F) | 0x80);
    value >>= 7;
  }
  output.push_back(value);
}

bool CompressedBG::decompress_stripe(uint32_t index,
                                      std::vector<uint8_t> &output) const {
  if (index >= nb_stripes) {
    fprintf(stderr, "Error: Invalid stripe index %u\n", index);
    return false;
  }

  const auto &stripe = stripes[index];
  if (stripe.first + 4 > _data.size()) {
    fprintf(stderr, "Error: Stripe %u header out of bounds.\n", index);
    return false;
  }

  uint32_t decompressed_size =
      le_to_host_u32(*reinterpret_cast<const uint32_t*>(
                          _data.data() + stripe.first));

  size_t pos = stripe.first + 4;

  ByteHuffmanTable huff_table;
  bool has_non_zero_weight = false;
  for (int i = 0; i < 256; ++i) {
    if (pos >= stripe.first + stripe.second) {
      fprintf(stderr, "Error: Incomplete Huffman table in "
                      "stripe %u at pos %zu\n", index, pos);
      return false;
    }
    uint32_t weight = read_variable(_data, pos, stripe.first + stripe.second);
    auto *node = huff_table.getNode(static_cast<uint8_t>(i));
    if (!node) {
      fprintf(stderr, "Error: No Huffman node for value=%u\n", i);
      return false;
    }
    node->weight = weight;
    if (weight > 0) {
      has_non_zero_weight = true;
    }
  }

  if (!has_non_zero_weight) {
    fprintf(stderr, "Error: No non-zero weights in Huffman "
                    "table for stripe %u\n", index);
    return false;
  }

  huff_table.buildTree(511);

  std::vector<uint8_t> compressed_data(_data.begin() + pos, _data.begin() +
                                        stripe.first + stripe.second);
  if (compressed_data.empty()) {
    fprintf(stderr, "Error: Empty compressed data for stripe %u\n", index);
    return false;
  }

  BitStreamReader bit_stream(compressed_data);
  std::vector<uint8_t> huff_output;
  huff_output.reserve(decompressed_size);

  for (uint32_t i = 0; i < decompressed_size; ++i) {
    uint8_t value = huff_table.decodeSequence(bit_stream);
    huff_output.push_back(value);
  }

  if (huff_output.size() != decompressed_size) {
    fprintf(stderr, "Error: Huffman output size mismatch in stripe %u: "
                    "expected %u, got %zu\n", index, decompressed_size,
                    huff_output.size());
    return false;
  }

  std::vector<uint8_t> decoded_pixels;
  bool zeros = false;
  pos = 0;
  while (pos < huff_output.size()) {
    uint32_t length = read_variable(huff_output, pos, huff_output.size());
    if (zeros) {
      decoded_pixels.insert(decoded_pixels.end(), length, 0);
    } else {
      if (pos + length > huff_output.size()) {
        fprintf(stderr, "Error: Invalid RLE data in stripe %u at "
                        "pos %zu, length %u\n", index, pos, length);
        return false;
      }
      decoded_pixels.insert(decoded_pixels.end(), huff_output.begin() + pos,
                            huff_output.begin() + pos + length);
      pos += length;
    }
    zeros = !zeros;
  }

  const uint32_t height = stripe_height(index);
  const uint32_t channels = header.bpp / 8;
  const size_t expected_size = header.width * height * channels;

  if (decoded_pixels.size() != expected_size) {
    fprintf(stderr, "Error: Decoded pixels size mismatch in stripe %u: "
                    "expected %zu, got %zu\n", index, expected_size,
                    decoded_pixels.size());
    return false;
  }

  // Differential decoding
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < header.width; ++x) {
      for (uint32_t c = 0; c < channels; ++c) {
        size_t idx = (y * header.width + x) * channels + c;
        if (y == 0 && x == 0) {
          continue;
        } else if (y == 0) {
          decoded_pixels[idx] += decoded_pixels[y * header.width * channels +
                                                (x - 1) * channels + c];
        } else if (x == 0) {
          decoded_pixels[idx] += decoded_pixels[(y - 1) * header.width *
                                                channels + x * channels + c];
        } else {
          decoded_pixels[idx] += ((uint16_t)decoded_pixels[(y - 1) *
                                  header.width * channels + x * channels + c] +
                                  (uint16_t)decoded_pixels[y * header.width *
                                  channels + (x - 1) * channels + c]) >> 1;
        }
      }
    }
  }

  // Color conversion (BGR -> RGB)
  if (header.bpp == 24 || header.bpp == 32) {
    for (size_t i = 0; i < decoded_pixels.size(); i += channels) {
      std::swap(decoded_pixels[i], decoded_pixels[i + 2]);
    }
  }

  output = std::move(decoded_pixels);
  return true;
}


bool CompressedBG::compress_stripe(const uint8_t *pixels, uint32_t index,
                                    std::vector<uint8_t> &output) const {
  if (index >= nb_stripes) {
    fprintf(stderr, "Error: Invalid stripe index %u\n", index);
    return false;
  }

  const uint32_t height = stripe_height(index);
  const uint32_t channels = header.bpp / 8;
  const uint32_t stride = header.width * channels;
  const uint32_t stripe_size = height * stride;

  if (pixels == nullptr) {
    fprintf(stderr, "Error: Null pixel data for stripe %u\n", index);
    return false;
  }

  // Copy pixel data
  std::vector<uint8_t> stripe_pixels(pixels, pixels + stripe_size);

  // Color conversion (RGB -> BGR)
  if (header.bpp == 24 || header.bpp == 32) {
    for (size_t i = 0; i < stripe_size; i += channels) {
      std::swap(stripe_pixels[i], stripe_pixels[i + 2]);
    }
  }

  // Differential encoding
  for (int32_t y = height - 1; y >= 1; --y) {
    for (int32_t x = header.width - 1; x >= 1; --x) {
      for (uint32_t c = 0; c < channels; ++c) {
        size_t current_index = y * stride + x * channels + c;
        size_t up_index = (y - 1) * stride + x * channels + c;
        size_t left_index = y * stride + (x - 1) * channels + c;
        uint32_t diff = stripe_pixels[current_index] -
                        ((static_cast<uint32_t>(stripe_pixels[up_index]) +
                        static_cast<uint32_t>(stripe_pixels[left_index])) >> 1);
        stripe_pixels[current_index] = static_cast<uint8_t>(diff);
      }
    }
  }

  for (int32_t y = height - 1; y >= 1; --y) {
    for (uint32_t c = 0; c < channels; ++c) {
      size_t current_index = y * stride + c;
      size_t up_index = (y - 1) * stride + c;
      uint32_t diff = stripe_pixels[current_index] - stripe_pixels[up_index];
      stripe_pixels[current_index] = static_cast<uint8_t>(diff);
    }
  }

  for (int32_t x = header.width - 1; x >= 1; --x) {
    for (uint32_t c = 0; c < channels; ++c) {
      size_t current_index = x * channels + c;
      size_t left_index = (x - 1) * channels + c;
      uint32_t diff = stripe_pixels[current_index] - stripe_pixels[left_index];
      stripe_pixels[current_index] = static_cast<uint8_t>(diff);
    }
  }

  // RLE encoding
  std::vector<uint8_t> rle_data;
  size_t cursor = 0;
  if (!stripe_pixels.empty() && stripe_pixels[0] == 0) {
    write_variable(rle_data, 0);
  }
  while (cursor < stripe_pixels.size()) {
    size_t run_start = cursor;
    // Non-zero run
    if (cursor < stripe_pixels.size() && stripe_pixels[cursor] != 0) {
      while (cursor < stripe_pixels.size() && stripe_pixels[cursor] != 0) {
        ++cursor;
      }
      const size_t run_length = cursor - run_start;
      write_variable(rle_data, run_length);
      rle_data.insert(rle_data.end(), stripe_pixels.begin() + run_start,
                      stripe_pixels.begin() + run_start + run_length);
    }
    // Zero run
    else {
      while (cursor < stripe_pixels.size() && stripe_pixels[cursor] == 0) {
        ++cursor;
      }
      const size_t run_length = cursor - run_start;
      write_variable(rle_data, run_length);
    }
  }

  // Huffman encoding
  ByteHuffmanTable huff_table;
  for (uint8_t byte : rle_data) {
    huff_table.getNode(byte)->weight++;
  }

  output.reserve(4 + 256 * 5);
  const uint32_t decompressed_size = rle_data.size();
  const uint32_t decompressed_size_le = host_to_le_u32(decompressed_size);
  output.insert(output.end(),
                reinterpret_cast<const uint8_t*>(&decompressed_size_le),
                reinterpret_cast<const uint8_t*>(&decompressed_size_le) + 4);

  for (int i = 0; i < 256; ++i) {
    write_variable(output, huff_table.getNode(i)->weight);
  }

  huff_table.buildTree(511);

  auto bit_writer = std::make_unique<BitStreamWriter>(
                        std::make_shared<std::vector<uint8_t>>());
  for (uint8_t byte : rle_data) {
    huff_table.encodeValue(*bit_writer, byte);
  }
  bit_writer->flush();

  const auto &compressed_data = *bit_writer->data();
  output.insert(output.end(), compressed_data.begin(), compressed_data.end());

  return true;
}

bool CompressedBG::img_write(std::string &output) const {
  if (stripes.size() != nb_stripes) {
    fprintf(stderr, "Error: Stripes size mismatch: expected %u, got %zu\n",
                    nb_stripes, stripes.size());
    return false;
  }
  if (_data.empty()) {
    fprintf(stderr, "Error: Empty CBG data.\n");
    return false;
  }

  std::vector<uint8_t> raw_pixels;
  const size_t pixel_size = static_cast<size_t>(header.width) *
                            header.height * (header.bpp / 8);
  try {
    raw_pixels.reserve(pixel_size);
  } catch (const std::bad_alloc &e) {
    fprintf(stderr, "Error: Failed to reserve %zu bytes for raw_pixels: %s\n",
                    pixel_size, e.what());
    return false;
  }

  for (uint32_t i = 0; i < nb_stripes; ++i) {
    std::vector<uint8_t> stripe;
    if (!decompress_stripe(i, stripe)) {
      fprintf(stderr, "Error: Failed to decompress stripe %u\n", i);
      return false;
    }
    raw_pixels.insert(raw_pixels.end(), stripe.begin(), stripe.end());
  }

  const size_t expected_size = static_cast<size_t>(header.width) *
                                header.height * (header.bpp / 8);
  if (raw_pixels.size() != expected_size) {
    fprintf(stderr, "Error: Raw pixels size mismatch: expected %zu, got %zu\n",
                    expected_size, raw_pixels.size());
    return false;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                            nullptr, nullptr);
  if (!png) {
    fprintf(stderr, "Error: Failed to create PNG write struct.\n");
    return false;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    fprintf(stderr, "Error: Failed to create PNG info struct.\n");
    return false;
  }

  output.clear();
  png_set_write_fn(png, &output, [](png_structp png,
                    png_bytep data, png_size_t length) {
    auto *out = static_cast<std::string*>(png_get_io_ptr(png));
    out->append(reinterpret_cast<char*>(data), length);
  }, nullptr);

  // png_set_compression_level(png, 9);

  int color_type;
  switch (header.bpp) {
    case 8: color_type = PNG_COLOR_TYPE_GRAY; break;
    case 24: color_type = PNG_COLOR_TYPE_RGB; break;
    case 32: color_type = PNG_COLOR_TYPE_RGBA; break;
    default:
      png_destroy_write_struct(&png, &info);
      fprintf(stderr, "Error: Unsupported BPP value %u\n", header.bpp);
      return false;
  }

  png_set_IHDR(png, info, header.width, header.height, 8, color_type,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  std::vector<png_bytep> rows(header.height);
  for (uint32_t y = 0; y < header.height; ++y) {
    rows[y] = raw_pixels.data() + y * header.width * (header.bpp / 8);
  }

  png_write_image(png, rows.data());
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);

  return true;
}

bool CompressedBG::img_read(const std::string &src) {
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                            nullptr, nullptr);
  if (!png) {
    fprintf(stderr, "Error: Failed to create PNG read struct.\n");
    return false;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    fprintf(stderr, "Error: Failed to create PNG info struct.\n");
    return false;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    fprintf(stderr, "Error: PNG read error.\n");
    return false;
  }

  const uint8_t *png_data = reinterpret_cast<const uint8_t*>(src.data());
  png_set_read_fn(png, (png_voidp)&png_data, [](png_structp png,
                  png_bytep out_bytes, png_size_t byte_count) {
    const uint8_t** png_data_ptr = (const uint8_t**)png_get_io_ptr(png);
    memcpy(out_bytes, *png_data_ptr, byte_count);
    *png_data_ptr += byte_count;
  });

  png_read_info(png, info);

  if (png_get_image_width(png, info) != header.width ||
      png_get_image_height(png, info) != header.height) {
    png_destroy_read_struct(&png, &info, nullptr);
    fprintf(stderr, "Error: PNG dimensions don't match CBG.\n");
    return false;
  }

  int color_type;
  switch (header.bpp) {
    case 8:
      color_type = PNG_COLOR_TYPE_GRAY;
      break;
    case 24:
      color_type = PNG_COLOR_TYPE_RGB;
      break;
    case 32:
      color_type = PNG_COLOR_TYPE_RGBA;
      break;
    default:
      png_destroy_read_struct(&png, &info, nullptr);
      fprintf(stderr, "Error: Unsupported BPP value %u\n", header.bpp);
      return false;
  }

  if (png_get_color_type(png, info) != color_type) {
    switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
        png_set_rgb_to_gray_fixed(png, 1, -1, -1);
        break;
      case PNG_COLOR_TYPE_RGB:
        if (png_get_color_type(png, info) == PNG_COLOR_TYPE_RGBA) {
          png_set_strip_alpha(png);
        }
        break;
      case PNG_COLOR_TYPE_RGBA:
        if (png_get_color_type(png, info) == PNG_COLOR_TYPE_RGB) {
          png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
        }
        break;
    }
  }

  png_set_expand(png);
  png_set_strip_16(png);
  png_read_update_info(png, info);

  int rowbytes = png_get_rowbytes(png, info);
  if (rowbytes <= 0) {
    fprintf(stderr, "Invalid row bytes.\n");
    png_destroy_read_struct(&png, &info, nullptr);
    return false;
  }

  std::vector<uint8_t> pixels(header.height * rowbytes);
  std::vector<png_bytep> rows(header.height);
  for (uint32_t y = 0; y < header.height; ++y) {
    rows[y] = pixels.data() + y * rowbytes;
  }

  png_read_image(png, rows.data());
  png_destroy_read_struct(&png, &info, nullptr);

  // Truncate _data to header size (48 + nb_stripes * 4)
  size_t header_size = 48 + nb_stripes * 4;
  if (_data.size() < header_size) {
    fprintf(stderr, "Error: Invalid CBG header size.\n");
    return false;
  }
  _data.resize(header_size);
  stripes.clear();
  stripes.resize(nb_stripes);

  // Compress each stripe and append to _data
  size_t current_offset = header_size;
  for (uint32_t i = 0; i < nb_stripes; ++i) {
    std::vector<uint8_t> compressed;
    const uint32_t row_start = i * header.stripe_h;
    const uint8_t *stripe_pixels = pixels.data() + row_start *
                                    header.width * (header.bpp / 8);
    if (!compress_stripe(stripe_pixels, i, compressed)) {
      fprintf(stderr, "Error: Failed to compress stripe %u\n", i);
      return false;
    }
    stripes[i] = {current_offset, compressed.size()};
    _data.insert(_data.end(), compressed.begin(), compressed.end());
    current_offset += compressed.size();
  }

  // Update stripe offsets in header (starting at position 48)
  for (uint32_t i = 0; i < nb_stripes; ++i) {
    uint32_t offset = host_to_le_u32(stripes[i].first);
    if (48 + i * 4 + 4 > _data.size()) {
      fprintf(stderr, "Error: Invalid header access for stripe %u\n", i);
      return false;
    }
    std::copy(reinterpret_cast<uint8_t*>(&offset),
              reinterpret_cast<uint8_t*>(&offset) + 4,
              _data.begin() + 48 + i * 4);
  }

  return true;
}

bool CompressedBG::cbg_write(std::string &dest) const {
  dest.assign(_data.begin(), _data.end());
  return true;
}

} // namespace mg::data