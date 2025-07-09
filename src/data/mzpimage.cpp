#include <mg/data/hep.hpp>
#include <mg/data/quantize.hpp>
#include <mg/data/mzpimage.hpp>
// #include <mg/util/fs.hpp>
// #include <mg/util/string.hpp>
// #include <filesystem>

namespace mg::data {

std::vector<uint8_t> rgb565_unpack(const std::vector<uint16_t> &pq,
                                   const std::vector<uint8_t> &offsets_byte) {
  if (pq.size() != offsets_byte.size()) {
    fprintf(stderr, "Error: rgb565_unpack - pq and offsets_byte must "
                    "have the same size.\n");
    return {};
  }

  std::vector<uint8_t> result(pq.size() * 3);

  for (size_t i = 0; i < pq.size(); ++i) {
    uint16_t pixel = pq[i];
    uint8_t offset = offsets_byte[i];

    // Unpack RGB565
    uint8_t r = ((pixel & 0xF800) >> 8) + (offset >> 5);
    uint8_t g = ((pixel & 0x07E0) >> 3) + ((offset >> 3) & 0x03);
    uint8_t b = ((pixel & 0x001F) << 3) + (offset & 0x07);

    result[i * 3] = r;
    result[i * 3 + 1] = g;
    result[i * 3 + 2] = b;
  }

  return result;
}

std::pair<std::vector<uint16_t>, std::vector<uint8_t>>
rgb565_pack(const std::vector<uint8_t> &rgb) {
  if (rgb.size() % 3 != 0) {
    fprintf(stderr, "Error: rgb size must be a multiple of 3.\n");
    return {};
  }

  std::vector<uint16_t> pq(rgb.size() / 3);
  std::vector<uint8_t> offsets(rgb.size() / 3);

  for (size_t i = 0; i < rgb.size(); i += 3) {
    uint8_t r = rgb[i];
    uint8_t g = rgb[i + 1];
    uint8_t b = rgb[i + 2];

    // Pack into RGB565
    uint16_t pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);

    // Calculate offset
    uint8_t offset = ((r & 0x07) << 5) | ((g & 0x03) << 3) | (b & 0x07);

    pq[i / 3] = pixel;
    offsets[i / 3] = offset;
  }

  return {pq, offsets};
}

MzpImage::MzpImage(const std::string &data) : header(header_) {
  if (!mzp_read(data, mzp_archive)) {
    fprintf(stderr, "Failed to read MZP archive.\n");
    return;
  }

  if (mzp_archive.entry_data.empty()) {
    fprintf(stderr, "MZP archive is empty.\n");
    return;
  }

  if (!read_image_header(mzp_archive.entry_data[0])) {
    fprintf(stderr, "Failed to read image header from MZP archive.\n");
    return;
  }

  bits_per_px = get_bits_per_px();
  channels = nb_channels();

  if (channels == -1) {
    fprintf(stderr, "Failed to get channels.\n");
    return;
  }

  is_valid_ = true;

  // int tiles_count = header_.tile_x_count * header_.tile_y_count;

  // fprintf(stderr, "Width=%u,Height=%u,Tile_Width=%u,Tile_Height=%u,"
  //                 "Tiles_X=%u,Tiles Y=%u,Tiles=%d,Tile_Crop=%u,"
  //                 "BMP_Type=0x%02X,BMP_Depth=0x%02X,BPP=%d,Channels=%d,"
  //                 "Entry_0_size=%zu\n", header_.width, header_.height,
  //                 header_.tile_width, header_.tile_height,
  //                 header_.tile_x_count, header_.tile_y_count, tiles_count,
  //                 header_.tile_crop, header_.bmp_type, header_.bmp_depth,
  //                 bits_per_px, channels, mzp_archive.entry_data[0].size());
}

bool MzpImage::read_image_header(const std::string &data) {
  if (data.size() < sizeof(MzpImageHeader)) {
    fprintf(stderr, "Data is too small to contain an image header.\n");
    return false;
  }

  // Cast data to MzpImageHeader
  header_ = *reinterpret_cast<const MzpImageHeader*>(data.data());

  // Convert little-endian to host order for uint16_t fields
  header_.width = le_to_host_u16(header_.width);
  header_.height = le_to_host_u16(header_.height);
  header_.tile_width = le_to_host_u16(header_.tile_width);
  header_.tile_height = le_to_host_u16(header_.tile_height);
  header_.tile_x_count = le_to_host_u16(header_.tile_x_count);
  header_.tile_y_count = le_to_host_u16(header_.tile_y_count);
  header_.bmp_type = le_to_host_u16(header_.bmp_type);
  // bmp_depth and tile_crop are uint8_t, no conversion needed

  return true;
}

// bool MzpImage::write_image_header(std::string &out) const {
//   // Clear output and reserve space for header
//   out.clear();
//   out.resize(sizeof(MzpImageHeader));

//   // Create header with converted fields
//   MzpImageHeader header = header_;
//   header.width = host_to_le_u16(header_.width);
//   header.height = host_to_le_u16(header_.height);
//   header.tile_width = host_to_le_u16(header_.tile_width);
//   header.tile_height = host_to_le_u16(header_.tile_height);
//   header.tile_x_count = host_to_le_u16(header_.tile_x_count);
//   header.tile_y_count = host_to_le_u16(header_.tile_y_count);
//   header.bmp_type = host_to_le_u16(header_.bmp_type);
//   // bmp_depth and tile_crop are uint8_t, no conversion needed

//   // Copy header to output
//   *reinterpret_cast<MzpImageHeader*>(out.data()) = header;

//   return true;
// }

int MzpImage::get_bits_per_px() const{
  if (header_.bmp_type == 0x01) {
    if (header_.bmp_depth == 0x00 || header_.bmp_depth == 0x10) {
      return 4;
    } else if (header_.bmp_depth == 0x01 ||
                header_.bmp_depth == 0x11 ||
                header_.bmp_depth == 0x91) {
      return 8;
    }
  } else if (header_.bmp_type == 0x08 && header_.bmp_depth == 0x14) {
    return 24;
  } else if (header_.bmp_type == 0x0B && header_.bmp_depth == 0x14) {
    return 32;
  } else if (header_.bmp_type == 0x0C && header_.bmp_depth == 0x11) {
    return 32;
  } else if (header_.bmp_type == 0x03) {
    fprintf(stderr, "Unsupported bmp type 0x03 (PEH).\n");
  } else {
    fprintf(stderr, "Unknown bmp type & depth pair %0X, %0X\n",
                    header_.bmp_type, header_.bmp_depth);
  }
  return 0;
}

int MzpImage::nb_channels() const {
  switch (header_.bmp_type) {
    case 0x01:
      if (bits_per_px == 4 || bits_per_px == 8) {
        return 1;
      }
      break;
    case 0x0C:
      return 4;
    default:
      if (bits_per_px == 24) {
        return 3;
      } else if (bits_per_px == 32) {
        return 4;
      } else {
        fprintf(stderr, "Error: Unexpected bmp type - bpp pair %0X, %d\n",
                        header_.bmp_type, bits_per_px);
        return -1;
      }
      break;
  }
  fprintf(stderr, "Error: Unexpected bmp type - bpp pair %0X, %d\n",
                  header_.bmp_type, bits_per_px);
  return -1;
}

int MzpImage::get_filler_index() {
  int filler;
  if (header_.bmp_type == 0x01) {
    // Extract filler from the last tile in mzp_archive
    if (!mzp_archive.entry_data.empty()) {
      const std::string &last_tile_compressed = mzp_archive.entry_data.back();

      // Check MzxHeader
      if (last_tile_compressed.size() < sizeof(MzxHeader)) {
        fprintf(stderr, "Last tile data too small to contain MzxHeader.\n");
        return 0;
      } else {
        MzxHeader header =
            *reinterpret_cast<const MzxHeader*>(last_tile_compressed.data());
        header.to_host_order();

        // Validate magic
        if (memcmp(header.magic, MzxHeader::FILE_MAGIC,
            sizeof(header.magic)) != 0) {
          fprintf(stderr, "Invalid file magic in last tile.\n");
          return 0;
        } else {
          // Decompress the tile
          std::string decompressed_tile;
          if (!mg::data::mzx_decompress(last_tile_compressed,
              decompressed_tile)) {
            fprintf(stderr, "Failed to decompress last tile.\n");
            return 0;
          } else {
            // Validate decompressed size
            if (decompressed_tile.size() != header.decompressed_size) {
              fprintf(stderr, "Decompressed tile size mismatch: "
                              "expected %u, got %zu\n",
                              header.decompressed_size,
                              decompressed_tile.size());
            } else if (decompressed_tile.empty()) {
              fprintf(stderr, "Decompressed tile is empty.\n");
              return 0;
            } else {
              // Get the last byte as filler index
              filler = static_cast<uint8_t>(decompressed_tile.back());
              // Validate filler index
              if (static_cast<size_t>(filler) >= palette.size()) {
                fprintf(stderr, "Invalid filler index from last "
                                "tile: %u (palette size: %zu)\n",
                                filler, palette.size());
                return 0;
              } else {
                return filler;
              }
            }
          }
        }
      }
    } else {
      fprintf(stderr, "MZP archive is empty, cannot extract filler.\n");
      return 0;
    }
  }
  return 0;
}

size_t MzpImage::get_tile_size() const {
  return header_.tile_height * header_.tile_width;
}

std::vector<MzpImage::Color> MzpImage::get_palette() {
  const std::string &data = mzp_archive.entry_data[0];
  std::vector<Color> palette;

  if (header_.bmp_type != 0x01) {
    fprintf(stderr, "Palette is not present for bmp_type: 0x%02X\n",
                    header_.bmp_type);
    return palette;
  }

  size_t palette_size = 0;
  if (header_.bmp_depth == 0x00 || header_.bmp_depth == 0x10) {
    palette_size = 16;
  } else if (header_.bmp_depth == 0x01 ||
              header_.bmp_depth == 0x11 ||
              header_.bmp_depth == 0x91) {
    palette_size = 256;
  } else {
    fprintf(stderr, "Unknown bmp_depth: 0x%02X\n", header_.bmp_depth);
    return palette;
  }

  const size_t palette_bytes = palette_size * 4;

  palette.reserve(palette_size);

  if (data.size() < 16 + palette_bytes) {
    fprintf(stderr, "Data is too small to contain a palette.\n");
    return palette;
  }

  for (size_t i = 0; i < palette_size; ++i) {
    size_t idx = 16 + i * 4;

    Color color(
      data[idx],        // R
      data[idx + 1],    // G
      data[idx + 2],    // B
      fix_alpha(data[idx + 3]) // A
    );

    palette.push_back(color);
  }

  // Rearrangement of palette (for bmp_depth 0x11 and 0x91)
  if (header_.bmp_depth == 0x11 || header_.bmp_depth == 0x91) {
    for (size_t i = 0; i < palette_size; i += 32) {
      std::swap_ranges(palette.begin() + i + 8,
                       palette.begin() + i + 16,
                       palette.begin() + i + 16);
    }
  }

  if (palette.size() < 256) {
    palette.resize(256, Color());
  }

  // for (size_t i = 0; i < palette.size(); ++i) {
  //   fprintf(stderr, "Color %zu R: %u, G: %u, B: %u, A: %u\n", i,
  //                   palette[i].r, palette[i].g, palette[i].b, palette[i].a);
  // }

  return palette;
}

void MzpImage::set_palette() {
  size_t palette_size = 0;

  switch (header_.bmp_type) {
    case 0x01:
      switch (header_.bmp_depth) {
        case 0x00:
        case 0x10:
          palette_size = 16;
          break;
        case 0x01:
        case 0x11:
        case 0x91:
          palette_size = 256;
          break;
        default:
          fprintf(stderr, "Unknown depth 0x%02X\n", header_.bmp_depth);
          return;
      }
      break;
    default:
      return; // No palette
  }

  if (palette.size() != palette_size) {
    fprintf(stderr, "Palette size mismatch: expected %zu, got %zu\n",
                    palette_size, palette.size());
    return;
  }

  std::vector<uint8_t> palette_bytes;
  for (size_t i = 0; i < palette.size(); ++i) {
    palette_bytes.push_back(palette[i].r);               // R
    palette_bytes.push_back(palette[i].g);               // G
    palette_bytes.push_back(palette[i].b);               // B
    palette_bytes.push_back(unfix_alpha(palette[i].a));  // A
  }

  if (header_.bmp_depth == 0x11 || header_.bmp_depth == 0x91) {
    // Swap palette blocks
    for (size_t i = 0; i < palette_bytes.size(); i += 32 * 4) {
      std::swap_ranges(palette_bytes.begin() + i + 8 * 4,
                        palette_bytes.begin() + i + 16 * 4,
                        palette_bytes.begin() + i + 16 * 4);
    }
  }

  // for (size_t i = 0; i < palette_bytes.size(); i += 4) {
  //   uint8_t red = palette_bytes[i];
  //   uint8_t green = palette_bytes[i + 1];
  //   uint8_t blue = palette_bytes[i + 2];
  //   uint8_t alpha = palette_bytes[i + 3];

  //   fprintf(stderr, "Color %zu: R=%d, G=%d, B=%d, A=%d\n",
  //                   i / 4, red, green, blue, alpha);
  // }

  // Replace the palette data in the archive
  if (!palette_bytes.empty()) {
    tiles[0].append(reinterpret_cast<const char*>(palette_bytes.data()),
                    palette_bytes.size()
    );
  }
}

std::vector<uint8_t> MzpImage::get_tile(size_t index) const {
  const auto &entry = mzp_archive.entry_data[index + 1];
  std::vector<uint8_t> tile_data;

  std::string decompressed_data;
  // fprintf(stderr, "Decompress tile %zu.\n", index + 1);
  if (!mzx_decompress(entry, decompressed_data)) {
    fprintf(stderr, "Failed to decompress MZX tile %zu\n", index + 1);
    return {};
  }

  if (header_.bmp_type == 0x0C) {
    if (!hep_extract_tile(decompressed_data, tile_data, *this)) {
      fprintf(stderr, "Failed to extract HEP tile %zu\n", index + 1);
      return {};
    }
    if (tile_data.size() != tile_size * 4) {
      fprintf(stderr, "HEP tile %zu size mismatch.\n", index + 1);
      return {};
    }
    return tile_data;
  } else {
    tile_data.assign(decompressed_data.begin(), decompressed_data.end());
  }

  switch (bits_per_px) {
    case 4: {
      std::vector<uint8_t> result(tile_data.size() * 2);
      for (size_t i = 0; i < tile_data.size(); ++i) {
        result[i * 2] = tile_data[i] & 0x0F;
        result[i * 2 + 1] = tile_data[i] >> 4;
      }
      return result;
    }
    case 8: {
      return tile_data;
    }
    case 24:
    case 32: {
      if (header_.bmp_type != 0x08 && header_.bmp_type != 0x0B) {
        fprintf(stderr, "Invalid bmp_type for RGB/RGBA tile %zu\n", index + 1);
        return {};
      }

      size_t tile_size_bytes = tile_size * (bits_per_px / 8);

      if (tile_data.size() < tile_size_bytes) {
        fprintf(stderr, "Buffer too small for tile %zu data.\n", index + 1);
        return {};
      }

      std::vector<uint16_t> rgb565(tile_size);
      std::vector<uint8_t> offsets(tile_size);

      memcpy(rgb565.data(), tile_data.data(), tile_size * sizeof(uint16_t));
      memcpy(offsets.data(), tile_data.data() + tile_size * 2,
              tile_size * sizeof(uint8_t));

      std::vector<uint8_t> pixels = rgb565_unpack(rgb565, offsets);

      if (bits_per_px == 32) {
        std::vector<uint8_t> alpha(tile_size);
        memcpy(alpha.data(), tile_data.data() + tile_size * 3,
                tile_size * sizeof(uint8_t));

        std::vector<uint8_t> rgba_pixels(tile_size * 4);
        for (size_t i = 0; i < tile_size; ++i) {
          rgba_pixels[i * 4] = pixels[i * 3];
          rgba_pixels[i * 4 + 1] = pixels[i * 3 + 1];
          rgba_pixels[i * 4 + 2] = pixels[i * 3 + 2];
          rgba_pixels[i * 4 + 3] = alpha[i];
        }
        return rgba_pixels;
      }
      return pixels;
    }
    default:
      fprintf(stderr, "Unexpected bits_per_px: %d in %zu tile.\n",
                      bits_per_px, index + 1);
      return {};
  }
}

void MzpImage::set_tile(int index, const std::vector<uint8_t> &pixels,
                                                int compression_level) {

  size_t nb_px = pixels.size() / channels;

  if (pixels.size() % channels != 0) {
    fprintf(stderr, "Pixel data size of tile %d is not "
                    "a multiple of channel count.\n", index + 1);
    return;
  }

  if (header_.bmp_type == 0x0C) {
    hep_insert_tile(*this, index, pixels, compression_level);
    return;
  }

  std::vector<uint8_t> tile_file;

  switch (bits_per_px) {
    case 4: {
      std::vector<uint8_t> packed_pixels(nb_px / 2);
      for (size_t i = 0; i < nb_px / 2; ++i) {
        uint8_t pixel1 = pixels[i * 2] & 0x0F;
        uint8_t pixel2 = pixels[i * 2 + 1] & 0x0F;
        packed_pixels[i] = pixel1 | (pixel2 << 4);
      }

      tile_file = packed_pixels;
      break;
    }
    case 8: {
      tile_file = pixels;
      break;
    }
    case 24:
    case 32: {
      if (header_.bmp_type != 0x08 && header_.bmp_type != 0x0B) {
        fprintf(stderr, "Unexpected bmp type for 24/32 bpp in tile %d\n",
                        index + 1);
        return;
      }

      std::vector<uint8_t> rgb_pixels;
      std::vector<uint8_t> alpha_pixels;

      if (bits_per_px == 32) {

        for (size_t i = 0; i < nb_px; ++i) {
          rgb_pixels.insert(rgb_pixels.end(), pixels.begin() + i * 4,
                            pixels.begin() + i * 4 + 3);
          alpha_pixels.push_back(pixels[i * 4 + 3]);
        }

      } else {
        rgb_pixels = pixels;
      }

      std::pair<std::vector<uint16_t>, std::vector<uint8_t>>
          rgb565_result = rgb565_pack(rgb_pixels);
      std::vector<uint16_t> rgb565_data = rgb565_result.first;
      std::vector<uint8_t> offsets = rgb565_result.second;

      tile_file.reserve(rgb565_data.size() * 2 + offsets.size() +
                        (bits_per_px == 32 ? alpha_pixels.size() : 0));

      for (uint16_t pixel : rgb565_data) {
          tile_file.push_back(static_cast<uint8_t>(pixel & 0xFF));
          tile_file.push_back(static_cast<uint8_t>((pixel >> 8) & 0xFF));
      }

      tile_file.insert(tile_file.end(), offsets.begin(), offsets.end());

      if (bits_per_px == 32) {
          tile_file.insert(tile_file.end(), alpha_pixels.begin(),
                            alpha_pixels.end());
      }
      break;
    }
    default:
      fprintf(stderr, "Unexpected %d bpp in tile %d\n", bits_per_px, index + 1);
      return;
  }

  std::string tile_string(tile_file.begin(), tile_file.end());
  std::string compressed_tile;
  mzx_compress(tile_string, compressed_tile, compression_level);
  tiles.push_back(compressed_tile);
}

bool MzpImage::write_mzp(std::string &output) {

  mzp_archive.entry_headers.clear();
  mzp_archive.entry_data.clear();

  for (const auto &tile : tiles) {
    std::string tile_data(tile.begin(), tile.end());
    mzp_archive.add_entry(tile_data);
  }

  mzp_write(mzp_archive, output);

  return true;
}

// bool save_tile(const std::vector<uint8_t> &tile_data, int x, int y,
//                 int tile_width, int tile_height, int channels,
//                 uint8_t transparency, const std::string &name = "") {
//   std::string tile_output;
//   png_structp tile_png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
//                                                 nullptr, nullptr, nullptr);
//   if (!tile_png) {
//     fprintf(stderr, "Failed to create PNG write struct for tile %d_%d.\n",
//                     y, x);
//     return false;
//   }

//   png_infop tile_info = png_create_info_struct(tile_png);
//   if (!tile_info) {
//     png_destroy_write_struct(&tile_png, nullptr);
//     fprintf(stderr, "Failed to create PNG info struct for tile %d_%d.\n", y, x);
//     return false;
//   }

//   if (setjmp(png_jmpbuf(tile_png))) {
//     png_destroy_write_struct(&tile_png, &tile_info);
//     fprintf(stderr, "Error during PNG tile %d_%d write.\n", y, x);
//     return false;
//   }

//   tile_output.clear();
//   png_set_write_fn(tile_png, &tile_output, [](png_structp png, png_bytep data,
//                     png_size_t length) {
//     auto *out = static_cast<std::string*>(png_get_io_ptr(png));
//     out->append(reinterpret_cast<char*>(data), length);
//   }, nullptr);

//   png_set_compression_level(tile_png, 9);
//   png_set_filter(tile_png, 0, PNG_FILTER_NONE);
//   png_set_IHDR(tile_png, tile_info, tile_width, tile_height, 8,
//                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
//                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

//   png_write_info(tile_png, tile_info);

//   std::vector<png_bytep> tile_rows(tile_height);
//   for (int ty = 0; ty < tile_height; ++ty) {
//     tile_rows[ty] = const_cast<png_bytep>(&tile_data[ty * tile_width *
//                                           channels]);
//   }

//   png_write_image(tile_png, tile_rows.data());
//   png_write_end(tile_png, nullptr);
//   png_destroy_write_struct(&tile_png, &tile_info);

//   std::string tile_filename = mg::string::format("%s_%d_%d_[%02u].png",
//                                                   name.c_str(), y, x,
//                                                   transparency);
//   std::filesystem::path tile_dir = "tiles";
//   if (!name.empty()) {
//     tile_dir /= name;
//   }

//   if (!tile_dir.empty() && !std::filesystem::is_directory(tile_dir)) {
//     if (!std::filesystem::create_directories(tile_dir)) {
//       fprintf(stderr, "Failed to create directory %s",
//                       tile_dir.string().c_str());
//     }
//   }
//   std::filesystem::path tile_path = tile_dir / tile_filename;
//   if (!mg::fs::write_file(tile_path.string().c_str(), tile_output)) {
//     return false;
//   }
//   return true;
// }

bool MzpImage::img_write(std::string &output, bool overlap) {
                          // const std::string &name) {
  tile_size = get_tile_size();
  int crop = header_.tile_crop; // Assume crop = 1 for 1-pixel overlap
  int tile_y_count = header_.tile_y_count;
  int tile_x_count = header_.tile_x_count;
  int width = header_.width - (tile_x_count - 1) * crop * 2 +
                (header_.bmp_type == 0x01 ? 1 : 0);
  int height = header_.height - (tile_y_count - 1) * crop * 2 +
                (header_.bmp_type == 0x01 ? 1 : 0);
  int tile_height = header_.tile_height;
  int tile_width = header_.tile_width;

  int result_width = width;
  int result_height = height;
  std::vector<uint8_t> result_image(result_width * result_height * channels);

  for (int y = 0; y < tile_y_count; ++y) {
    int start_row = y * (tile_height - crop * 2);
    int start_y = (!overlap && crop && y > 0) ? crop : 0;
    int end_y = (!overlap && crop && y < tile_y_count - 1) ?
                  tile_height - crop : tile_height;
    int row_count = std::min(height - start_row, end_y - start_y);

    for (int x = 0; x < tile_x_count; ++x) {
      int index = tile_x_count * y + x;
      const std::vector<uint8_t> &tile_data = get_tile(index);
      int start_col = x * (tile_width - crop * 2);
      int start_x = (!overlap && crop && x > 0) ? crop : 0;
      int end_x = (!overlap && crop && x < tile_x_count - 1) ?
                    tile_width - crop : tile_width;
      int col_count = std::min(width - start_col, end_x - start_x);

      // Save tile if bmp_type is 0x0C
      // if (header_.bmp_type == 0x0C) {
      //   save_tile(tile_data, x, y, tile_width, tile_height, channels,
      //             mzp_archive.entry_data[0][16 + index], name);
      // }

      // Copy tile data to result image
      for (int tile_y = 0; tile_y < row_count; ++tile_y) {
        for (int tile_x = 0; tile_x < col_count; ++tile_x) {
          int source_index = ((tile_y + start_y) * tile_width +
                              (tile_x + start_x)) * channels;
          int dest_index = ((start_row + tile_y + start_y) * width +
                            (start_col + tile_x + start_x)) * channels;
          std::memcpy(&result_image[dest_index], &tile_data[source_index],
                      channels);
        }
      }
    }
  }

  // Write the full image
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                            nullptr, nullptr, nullptr);
  if (!png) {
    fprintf(stderr, "Failed to create PNG write struct.\n");
    return false;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    fprintf(stderr, "Failed to create PNG info struct.\n");
    return false;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fprintf(stderr, "Error during PNG write.\n");
    return false;
  }

  output.clear();
  png_set_write_fn(png, &output, [](png_structp png, png_bytep data,
                    png_size_t length) {
    auto *out = static_cast<std::string*>(png_get_io_ptr(png));
    out->append(reinterpret_cast<char*>(data), length);
  }, nullptr);

  png_set_compression_level(png, 9);

  png_set_filter(png, 0, PNG_FILTER_NONE);

  png_byte color_type = PNG_COLOR_TYPE_RGBA;
  if (header_.bmp_type == 0x01) {
    color_type = PNG_COLOR_TYPE_PALETTE;
  } else if (channels == 3) {
    color_type = PNG_COLOR_TYPE_RGB;
  } else if (channels == 1) {
    color_type = PNG_COLOR_TYPE_GRAY;
  }

  png_set_IHDR(png, info, width, height, 8, color_type, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  if (header_.bmp_type == 0x01) {
    palette = get_palette();
    if (palette.empty()) {
      fprintf(stderr, "Failed to read palette.\n");
      return false;
    }

    std::vector<png_color> plte(palette.size());
    std::vector<uint8_t> trns(palette.size());

    for (size_t i = 0; i < palette.size(); ++i) {
      plte[i].red = palette[i].r;
      plte[i].green = palette[i].g;
      plte[i].blue = palette[i].b;
      trns[i] = palette[i].a;
    }
    png_set_PLTE(png, info, plte.data(), plte.size());
    png_set_tRNS(png, info, trns.data(), trns.size(), nullptr);
  }

  png_write_info(png, info);

  std::vector<png_bytep> row_pointers(height);
  for (int y = 0; y < height; y++) {
    row_pointers[y] = &result_image[y * result_width * channels];
  }

  png_write_image(png, row_pointers.data());
  png_write_end(png, nullptr);

  png_destroy_write_struct(&png, &info);

  return true;
}

bool MzpImage::img_read(const std::string &png_data, int compression_level,
                                                              bool overlap) {

  int crop = header_.tile_crop;
  int tile_y_count = header_.tile_y_count;
  int tile_x_count = header_.tile_x_count;
  int height = header_.height - (tile_y_count - 1) * crop * 2 +
                (header_.bmp_type == 0x01 ? 1 : 0);
  int width = header_.width - (tile_x_count - 1) * crop * 2 +
                (header_.bmp_type == 0x01 ? 1 : 0);
  int tile_height = header_.tile_height;
  int tile_width = header_.tile_width;

  tiles.clear();
  tiles.push_back(mzp_archive.entry_data[0].substr(0, 16));

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                            nullptr, nullptr, nullptr);
  if (!png) {
    fprintf(stderr, "Error creating PNG read struct.\n");
    return false;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    fprintf(stderr, "Error creating PNG info struct.\n");
    return false;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    fprintf(stderr, "Error during PNG decoding.\n");
    return false;
  }

  const uint8_t *png_data_ptr =
      reinterpret_cast<const uint8_t*>(png_data.data());
  png_set_read_fn(png, (png_voidp)&png_data_ptr, [](png_structp png,
                  png_bytep out_bytes, png_size_t byte_count) {
    const uint8_t** png_data_ptr_ptr = (const uint8_t**)png_get_io_ptr(png);
    memcpy(out_bytes, *png_data_ptr_ptr, byte_count);
    *png_data_ptr_ptr += byte_count;
  });

  png_read_info(png, info);

  int img_width = png_get_image_width(png, info);
  int img_height = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  if (img_width <= 0 || img_height <= 0) {
    fprintf(stderr, "Invalid image dimensions.\n");
    png_destroy_read_struct(&png, &info, nullptr);
    return false;
  }

  // Normalize bit depth
  if (bit_depth == 16) {
    png_set_strip_16(png);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }

  // Convert to target format
  if (color_type == PNG_COLOR_TYPE_GRAY) {
    png_set_gray_to_rgb(png);
    color_type = PNG_COLOR_TYPE_RGB;
  }

  // Handle palette images
  if (!(header_.bmp_type == 0x01 && (bits_per_px == 4 || bits_per_px == 8)) &&
        color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
    color_type = PNG_COLOR_TYPE_RGB;

    // For 32bpp: add alpha from tRNS if exists
    if (bits_per_px == 32 && png_get_valid(png, info, PNG_INFO_tRNS)) {
      png_set_tRNS_to_alpha(png);
      color_type = PNG_COLOR_TYPE_RGBA;
    }
  }

  // Handle Gray+Alpha
  if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA && bits_per_px == 32) {
    png_set_gray_to_rgb(png);
    color_type = PNG_COLOR_TYPE_RGB;

    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
      png_bytep trans_alpha;
      int num_trans;
      if (png_get_tRNS(png, info, &trans_alpha, &num_trans, nullptr)) {
        png_set_tRNS_to_alpha(png);
      }
      color_type = PNG_COLOR_TYPE_RGBA;
    }
  }

  // Final format adjustments
  if ((header_.bmp_type == 0x01 || bits_per_px == 32) &&
        color_type == PNG_COLOR_TYPE_RGB) {
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    color_type = PNG_COLOR_TYPE_RGBA;
  } else if (bits_per_px == 24 && color_type == PNG_COLOR_TYPE_RGBA) {
    png_set_strip_alpha(png);
    color_type = PNG_COLOR_TYPE_RGB;
  }

  // Verify final format
  png_read_update_info(png, info);

  int rowbytes = png_get_rowbytes(png, info);
  if (rowbytes <= 0) {
    fprintf(stderr, "Invalid row bytes.\n");
    png_destroy_read_struct(&png, &info, nullptr);
    return false;
  }

  std::vector<uint8_t> image_data(img_height * rowbytes);
  std::vector<png_bytep> row_pointers(img_height);

  for (int y = 0; y < img_height; y++) {
    row_pointers[y] = image_data.data() + y * rowbytes;
  }

  png_read_image(png, row_pointers.data());
  png_read_end(png, info);

  std::vector<uint8_t> img_pixels;

  int filler_color = 0;

  if (header_.bmp_type == 0x01 && (bits_per_px == 4 || bits_per_px == 8)) {
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
      int num_palette = 0;
      png_colorp palette_ptr = nullptr;
      png_get_PLTE(png, info, &palette_ptr, &num_palette);

      // fprintf(stderr, "PNG contains palette: %d colors.\n", num_palette);

      png_bytep trans_alpha = nullptr;
      int num_trans = 0;
      bool has_transparency = png_get_tRNS(png, info, &trans_alpha,
                                            &num_trans, nullptr);

      // fprintf(stderr, "PNG %s tRNS chunk, %d colors.\n",
      //                 has_transparency ? "contains" :
      //                 "does not contain", num_trans);

      for (int i = 0; i < num_palette; ++i) {
        Color color(
          palette_ptr[i].red,
          palette_ptr[i].green,
          palette_ptr[i].blue,
          (has_transparency && i < num_trans) ? trans_alpha[i] : 255
        );
        palette.push_back(color);
      }

      img_pixels = image_data;
      filler_color = get_filler_index();
    } else {
      size_t palette_size = (bits_per_px == 4) ? 16 : 256;
      // fprintf(stderr, "PNG does not contain palette (color type: %d).\n",
      //                 color_type);
      std::pair<std::vector<uint8_t>, std::vector<Color>>
          quantize_result = quantize_image_imagequant(image_data, img_width,
                                                      img_height, palette_size);

      img_pixels = quantize_result.first;

      for (size_t i = 0; i < quantize_result.second.size(); ++i) {
        Color color(
          quantize_result.second[i].r,
          quantize_result.second[i].g,
          quantize_result.second[i].b,
          quantize_result.second[i].a
        );
        palette.push_back(color);
      }

      if (palette.size() < palette_size) {
          palette.resize(palette_size, Color());
      }
    }

    // fprintf(stderr, "Global palette size: %zu\n", palette.size());
    set_palette();
  } else {
    if ((header_.bmp_type == 0x0C && bits_per_px == 32) ||
        (bits_per_px == 24) || (bits_per_px == 32)) {
      img_pixels = image_data;
    } else {
      fprintf(stderr, "Unexpected bmp type - bpp pair 0x%02X, %d\n",
                      header_.bmp_type, bits_per_px);
      return false;
    }
  }

  png_destroy_read_struct(&png, &info, nullptr);

  if (!(img_width == width && img_height == height)) {
    fprintf(stderr, "Image dimensions %d x %d do not match "
                    "local %d x %d dimensions.\n", img_width, img_height,
                    width, height);
    return false;
  }

  for (int y = 0; y < tile_y_count; ++y) {
    // Calculate transparent borders for non-overlapping case
    int top_border = (overlap || !crop || y == 0) ? 0 : 1;
    int bottom_border = (overlap || !crop || y == tile_y_count - 1) ? 0 : 1;

    // Calculate start and end rows, including overlap if enabled
    int start_row = y * (tile_height - crop * 2) + (overlap ? 0 : top_border);
    int inner_height = tile_height - (overlap ? 0 : top_border) -
                        (overlap ? 0 : bottom_border);
    int end_row = std::min(height, start_row + inner_height);
    int row_count = end_row - start_row;

    for (int x = 0; x < tile_x_count; ++x) {
      // Calculate transparent borders for non-overlapping case
      int left_border = (overlap || !crop || x == 0) ? 0 : 1;
      int right_border = (overlap || !crop || x == tile_x_count - 1) ? 0 : 1;

      // Calculate start and end columns, including overlap if enabled
      int index = tile_x_count * y + x;
      int start_col = x * (tile_width - crop * 2) + (overlap ? 0 : left_border);
      int inner_width = tile_width - (overlap ? 0 : left_border) -
                        (overlap ? 0 : right_border);
      int end_col = std::min(width, start_col + inner_width);
      int col_count = end_col - start_col;

      // Initialize tile with transparent black
      std::vector<uint8_t> tile_pixels(tile_width * tile_height * channels);
      bool all_transparent = true, any_transparent = false;

      // Copy pixels to tile inner area
      for (int r = 0; r < row_count; ++r) {
        for (int c = 0; c < col_count; ++c) {
          int pixel_idx = ((start_row + r) * width + start_col + c) * channels;
          int tile_idx = ((r + top_border) * tile_width + c + left_border) *
                          channels;
          std::memcpy(tile_pixels.data() + tile_idx,
                      img_pixels.data() + pixel_idx, channels);

          // Check transparency for inner pixels only
          uint8_t alpha = 255;
          if (channels == 4) {
            alpha = img_pixels[pixel_idx + 3];
          } else if (channels == 1 && !palette.empty() &&
                      img_pixels[pixel_idx] < palette.size()) {
            alpha = palette[img_pixels[pixel_idx]].a;
          } else {
            all_transparent = false;
            continue;
          }
          if (alpha < 255) any_transparent = true;
          if (alpha > 0) all_transparent = false;
        }
      }

      // Set transparency borders to transparent black
      if (!overlap && crop > 0) {
        if (top_border) {
          std::memset(tile_pixels.data(), 0,
                      tile_width * top_border * channels);
        }
        if (bottom_border) {
          std::memset(tile_pixels.data() + (tile_height - bottom_border) *
                      tile_width * channels, 0,
                      tile_width * bottom_border * channels);
        }
        if (left_border) {
          for (int r = 0; r < tile_height; ++r) {
            std::memset(tile_pixels.data() + (r * tile_width) * channels, 0,
                        left_border * channels);
          }
        }
        if (right_border) {
          for (int r = 0; r < tile_height; ++r) {
            std::memset(tile_pixels.data() + (r * tile_width + tile_width -
                        right_border) * channels, 0, right_border * channels);
          }
        }
      }

      // Compute transparency
      uint8_t transparency = all_transparent ? 0x00 :
                              any_transparent ? 0x01 : 0x02;
      uint8_t tile_transparency = (!overlap && crop && transparency == 0x02) ?
                                  0x01 : transparency;
      tiles[0].push_back(static_cast<char>(tile_transparency));
      int filler = (transparency != 0x02 && header_.bmp_type == 0x01) ?
                    filler_color : 0;

      // Fill empty areas if tile is partially outside image bounds
      if (row_count < inner_height || col_count < inner_width) {
        for (int r = top_border; r < top_border + inner_height; ++r) {
          for (int c = left_border; c < left_border + inner_width; ++c) {
            if (r >= top_border + row_count || c >= left_border + col_count) {
              int tile_idx = (r * tile_width + c) * channels;
              std::memset(tile_pixels.data() + tile_idx, filler, channels);
            }
          }
        }
      }

      set_tile(index, tile_pixels, compression_level);
    }
  }

  return true;
}

} // namespace mg::data
