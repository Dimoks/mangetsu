#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <mg/util/endian.hpp>
#include <mg/data/mzx.hpp>
#include <mg/data/mzpimage.hpp>
#include <mg/data/quantize.hpp>

namespace mg::data {

/* Header : 32 bytes:

0x00:0x03   4 magic bytes: HEP\0
0x04:0x07   4 bytes of file size (little endian,
            from start of header to last byte of file)
0x08:0x0F   8 bytes unknown (e.g. 00 00 30 25 06 00 00 00 /
            00 00 30 65 06 00 00 00 / 00 00 30 1D 06 00 00 00 /
            00 00 30 E1 05 00 00 00), seems to depend on dimensions
0x10:0x13   4 bytes unknown (e.g., 10 00 00 00)
0x14:0x17   4 bytes of width in little endian
0x18:0x1B   4 bytes of heigh in little endian
0x1C:0x1F   4 bytes unknown (e.g., 02 00 00 00 / 02 00 00 00)*/

constexpr uint32_t HEP_MAGIC = 0x00504548; // "HEP\0" in little-endian
constexpr uint32_t HEP_HEADER_SIZE = 0x20;
constexpr uint32_t HEP_PALETTE_SIZE = 0x400; // (RGBA)

uint8_t fix_alpha(uint8_t a) {
  if ((a & 0x80) == 0) {
    return ((a << 1) | (a >> 6)) & 0xFF;
  } else {
    return 0xFF;
  }
}

uint8_t unfix_alpha(uint8_t a) {
  return a >> 1;
}

bool hep_extract_tile(const std::string &data,
                      std::vector<uint8_t> &tile,
                            const MzpImage &mzp) {

  if (data.size() < HEP_HEADER_SIZE + HEP_PALETTE_SIZE) {
    fprintf(stderr, "Data is too small to contain HEP header and palette. "
                    "Data size: %zu\n", data.size());
    return false;
  }

  uint32_t magic =
      le_to_host_u32(*reinterpret_cast<const uint32_t *>(data.data()));
  if (magic != HEP_MAGIC) {
    fprintf(stderr, "Invalid HEP magic. Expected: 0x%08X, Got: 0x%08X\n",
                    HEP_MAGIC, magic);
    return false;
  }

  uint32_t file_size, width, height;
  memcpy(&file_size, data.data() + 4, 4);
  file_size = le_to_host_u32(file_size);

  memcpy(&width, data.data() + 20, 4);
  width = le_to_host_u32(width);

  memcpy(&height, data.data() + 24, 4);
  height = le_to_host_u32(height);

  if (width != mzp.header.tile_width || height != mzp.header.tile_height) {
    fprintf(stderr, "Tile dimensions mismatch. Expected: %dx%d, Got: %dx%d\n",
                    mzp.header.tile_width, mzp.header.tile_height,
                    width, height);
    return false;
  }

  if (file_size != HEP_HEADER_SIZE + width * height + HEP_PALETTE_SIZE) {
    fprintf(stderr, "File size mismatch. Expected: %u, Got: %u\n",
                    HEP_HEADER_SIZE + width * height + HEP_PALETTE_SIZE,
                    file_size);
    return false;
  }

  std::vector<uint8_t> palette(HEP_PALETTE_SIZE);
  memcpy(palette.data(), data.data() + HEP_HEADER_SIZE + width * height,
          HEP_PALETTE_SIZE);

  for (size_t i = 0; i < 256; ++i) {
    palette[i * 4 + 3] = fix_alpha(palette[i * 4 + 3]);
  }

  tile.resize(width * height * 4); // RGBA
  for (size_t i = 0; i < width * height; ++i) {
    uint8_t palette_index = data[HEP_HEADER_SIZE + i];
    tile[i * 4] = palette[palette_index * 4];         // R
    tile[i * 4 + 1] = palette[palette_index * 4 + 1]; // G
    tile[i * 4 + 2] = palette[palette_index * 4 + 2]; // B
    tile[i * 4 + 3] = palette[palette_index * 4 + 3]; // A
  }

  return true;
}

void hep_insert_tile(MzpImage &mzp, int tile_index,
                      const std::vector<uint8_t> &pixels,
                                int compression_level = 0) {
  int tile_width = mzp.header.tile_width;
  int tile_height = mzp.header.tile_height;

  // Calculate row and column in the tile grid
  int row = tile_index / mzp.header.tile_x_count;
  int col = tile_index % mzp.header.tile_x_count;

  // Set edge flags
  EdgeFlags flags;
  flags.left = (col == 0);
  flags.right = (col == mzp.header.tile_x_count - 1);
  flags.top = (row == 0);
  flags.bottom = (row == mzp.header.tile_y_count - 1);

  auto [indices, palette_rgba] =
      quantize_image_imagequant(pixels, tile_width, tile_height, 256, &flags);

  std::vector<std::vector<uint8_t>> palette(256, std::vector<uint8_t>(4, 0));
  for (size_t i = 0; i < 256; ++i) {
    if (i < palette_rgba.size()) {
      const MzpImage::Color &color = palette_rgba[i];
      palette[i][0] = color.r;
      palette[i][1] = color.g;
      palette[i][2] = color.b;
      palette[i][3] = unfix_alpha(color.a);
    } else {
      palette[i][0] = 0;
      palette[i][1] = 0;
      palette[i][2] = 0;
      palette[i][3] = unfix_alpha(0xFF);
    }
  }

  std::string compressed_tile;
  if (!mzp.getTileData(tile_index + 1, compressed_tile)) {
    fprintf(stderr, "Failed to get tile %d data.\n", tile_index + 1);
    return;
  }

  std::string decompressed_tile;
  // fprintf(stderr, "Decompress tile %d.\n", tile_index + 1);
  if (!mzx_decompress(compressed_tile, decompressed_tile)) {
    fprintf(stderr, "Failed to decompress tile %d.\n", tile_index + 1);
    return;
  }

  size_t offset = HEP_HEADER_SIZE;
  size_t data_size = tile_width * tile_height + 256 * 4;

  if (decompressed_tile.size() < offset + data_size) {
    fprintf(stderr, "Decompressed tile %d size is too small.\n",
                    tile_index + 1);
    return;
  }

  if (indices.size() != static_cast<size_t>(tile_width * tile_height)) {
    fprintf(stderr, "Mismatched indices of tile %d count: expected %d "
                    "(for %dx%d tile), got %zu\n", tile_index + 1,
                    tile_width * tile_height, tile_width,
                    tile_height, indices.size());
    return;
  }

  std::memcpy(decompressed_tile.data() + offset,
              indices.data(), indices.size());

  offset += indices.size();

  for (int i = 0; i < 256; ++i) {
    std::memcpy(decompressed_tile.data() + offset, palette[i].data(), 4);
    offset += 4;
  }

  if (offset != HEP_HEADER_SIZE + tile_width * tile_height + 256 * 4) {
    fprintf(stderr, "Offset of tile %d mismatch.\n", tile_index + 1);
    return;
  }

  std::string compressed_updated_tile;
  // fprintf(stderr, "Compress tile %d.\n", tile_index + 1);
  if (!mzx_compress(decompressed_tile, compressed_updated_tile,
                    compression_level)) {
    fprintf(stderr, "Failed to compress updated tile %d.\n", tile_index + 1);
    return;
  }

  mzp.addTile(compressed_updated_tile);
}

} // namespace mg::data
