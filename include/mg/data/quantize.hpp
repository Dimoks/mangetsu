#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>
#include <libimagequant.h>
#include <mg/data/mzpimage.hpp>

namespace mg::data {

// Structure to hold edge flags for a tile
struct EdgeFlags {
  bool left;
  bool right;
  bool top;
  bool bottom;
};

std::string get_error_message(liq_error code) {
  switch (code) {
    case LIQ_OK:
      return "Success.";
    case LIQ_QUALITY_TOO_LOW:
      return "Quality too low.";
    case LIQ_VALUE_OUT_OF_RANGE:
      return "Value out of range.";
    case LIQ_OUT_OF_MEMORY:
      return "Out of memory.";
    case LIQ_ABORTED:
      return "Aborted.";
    case LIQ_BITMAP_NOT_AVAILABLE:
      return "Bitmap not available.";
    case LIQ_BUFFER_TOO_SMALL:
      return "Buffer too small.";
    case LIQ_INVALID_POINTER:
      return "Invalid pointer.";
    case LIQ_UNSUPPORTED:
      return "Unsupported operation.";
    default:
      return "Unknown error: " + std::to_string(code);
  }
}

// Create importance map for libimagequant
std::vector<unsigned char> create_importance_map(int width, int height,
                                                  const EdgeFlags &edge_flags) {
  // Validate inputs
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "Invalid dimensions: width=%d, height=%d\n", width, height);
    return {};
  }

  uint8_t center_weight = 255;
  uint8_t inner_weight = 255;
  uint8_t outer_weight = 1;

  // Initialize importance map with minimal weight
  std::vector<uint8_t> map(width * height, outer_weight);

  // Set weights for central zone and inner edges if tile is large enough
  // Central zone (2 pixels inset)
  for (int y = 2; y < height - 2; ++y) {
    for (int x = 2; x < width - 2; ++x) {
      map[y * width + x] = center_weight;
    }
  }

  // Inner edges (1 pixel inset)
  for (int y = 1; y < height - 1; ++y) {
    map[y * width + 1] = inner_weight;            // Left inner
    map[y * width + (width - 2)] = inner_weight;  // Right inner
  }
  for (int x = 1; x < width - 1; ++x) {
    map[1 * width + x] = inner_weight;             // Top inner
    map[(height - 2) * width + x] = inner_weight;  // Bottom inner
  }
  // Inner edge corners
  map[1 * width + 1] = inner_weight;
  map[1 * width + (width - 2)] = inner_weight;
  map[(height - 2) * width + 1] = inner_weight;
  map[(height - 2) * width + (width - 2)] = inner_weight;

  // Override weights for edges on image boundary
  if (edge_flags.left) {
    for (int y = 0; y < height; ++y) {
      map[y * width] = center_weight;                      // Outer left edge
      if (width >= 2) map[y * width + 1] = center_weight;  // Inner left edge
    }
  }
  if (edge_flags.right) {
    for (int y = 0; y < height; ++y) {
      map[y * width + (width - 1)] = center_weight;  // Outer right edge
      // Inner right edge
      if (width >= 2) map[y * width + (width - 2)] = center_weight;
    }
  }
  if (edge_flags.top) {
    for (int x = 0; x < width; ++x) {
      map[x] = center_weight;                               // Outer top edge
      if (height >= 2) map[1 * width + x] = center_weight;  // Inner top edge
    }
  }
  if (edge_flags.bottom) {
    for (int x = 0; x < width; ++x) {
      map[(height - 1) * width + x] = center_weight;  // Outer bottom edge
      // Inner bottom edge
      if (height >= 2) map[(height - 2) * width + x] = center_weight;
    }
  }

  return map;
}

std::pair<std::vector<uint8_t>, std::vector<MzpImage::Color>>
quantize_image_imagequant(const std::vector<uint8_t> &image_data,
                          int width, int height, int max_colors,
                          const EdgeFlags *edge_flags = nullptr) {
  // Check image_data size for RGBA format
  size_t expected_size = static_cast<size_t>(width * height * 4);
  if (image_data.size() < expected_size) {
    fprintf(stderr, "Error: image_data size (%zu) less than expected (%zu)\n",
                    image_data.size(), expected_size);
    return {};
  }

  // Count unique colors
  std::map<uint32_t, size_t> color_map;
  for (size_t i = 0; i < image_data.size(); i += 4) {
    // Pack RGBA into uint32_t for color counting
    uint32_t color = (static_cast<uint32_t>(image_data[i]) << 24) |
                      (static_cast<uint32_t>(image_data[i + 1]) << 16) |
                      (static_cast<uint32_t>(image_data[i + 2]) << 8) |
                      static_cast<uint32_t>(image_data[i + 3]);
    ++color_map[color];
  }

  // If unique colors <= max_colors, create palette
  // directly without quantization
  if (color_map.size() <= static_cast<size_t>(max_colors)) {
    std::vector<MzpImage::Color> palette_rgba;
    palette_rgba.reserve(color_map.size());
    std::map<uint32_t, uint8_t> color_to_index;
    uint8_t index = 0;
    for (const auto &[color, _] : color_map) {
      palette_rgba.emplace_back(
        (color >> 24) & 0xFF, (color >> 16) & 0xFF,
        (color >> 8) & 0xFF, color & 0xFF);
      color_to_index[color] = index++;
    }

    // Generate indices for each pixel
    std::vector<uint8_t> indices(width * height);
    for (size_t i = 0, pixel = 0; i < image_data.size(); i += 4, ++pixel) {
      uint32_t color = (static_cast<uint32_t>(image_data[i]) << 24) |
                       (static_cast<uint32_t>(image_data[i + 1]) << 16) |
                       (static_cast<uint32_t>(image_data[i + 2]) << 8) |
                       static_cast<uint32_t>(image_data[i + 3]);
      indices[pixel] = color_to_index[color];
    }

    // fprintf(stderr, "Palette created without quantization (%zu colors).\n",
    //                 palette_rgba.size());

    return {indices, palette_rgba};
  }

  // Initialize libimagequant attributes for quantization
  int min_quality = 0;
  int max_quality = 100;
  float dithering = 1.0;

  liq_attr *attr = liq_attr_create();
  if (!attr) {
    fprintf(stderr, "Error: failed to create liq_attr.\n");
    return {};
  }

  liq_error error = liq_set_max_colors(attr, max_colors);
  if (error != LIQ_OK) {
    fprintf(stderr, "Failed to set max color: %s\n",
                    get_error_message(error).c_str());
    liq_attr_destroy(attr);
    return {};
  }
  error = liq_set_quality(attr, min_quality, max_quality);
  if (error != LIQ_OK) {
    fprintf(stderr, "Failed to set quality: %s\n",
                    get_error_message(error).c_str());
    liq_attr_destroy(attr);
    return {};
  }

  const unsigned char *input_data = image_data.data();
  liq_image *img = liq_image_create_rgba(attr, input_data, width, height, 0);
  if (!img) {
    fprintf(stderr, "Error: failed to create liq_image.\n");
    liq_attr_destroy(attr);
    return {};
  }
  // Add a transparent black color (0, 0, 0, 0)
  // to avoid replacing it with 71, 112, 76, 0 (hex: 47, 70, 4C, 00)
  liq_color black_transparent = {0, 0, 0, 0};
  error = liq_image_add_fixed_color(img, black_transparent);
  if (error != LIQ_OK) {
    fprintf(stderr, "Failed to add fixed color: %s\n",
                    get_error_message(error).c_str());
    liq_image_destroy(img);
    liq_attr_destroy(attr);
    return {};
  }

  // Set importance map if edge_flags is provided
  if (edge_flags && (width >= 10 && height >= 10)) {
    auto importance_map = create_importance_map(width, height, *edge_flags);
    if (importance_map.empty()) {
      fprintf(stderr, "Failed to create importance map.\n");
      liq_image_destroy(img);
      liq_attr_destroy(attr);
      return {};
    }
    error = liq_image_set_importance_map(
        img, importance_map.data(), width * height, LIQ_COPY_PIXELS);
    if (error != LIQ_OK) {
      fprintf(stderr, "Failed to set importance map: %s\n",
                       get_error_message(error).c_str());
      liq_image_destroy(img);
      liq_attr_destroy(attr);
      return {};
    }
  }

  liq_result *res;

  error = liq_image_quantize(img, attr, &res);
  if (error != LIQ_OK) {
    fprintf(stderr, "Error during quantization: %s\n",
                    get_error_message(error).c_str());
    liq_attr_destroy(attr);
    liq_image_destroy(img);
    return {};
  }

  error = liq_set_dithering_level(res, dithering);
  if (error != LIQ_OK) {
    fprintf(stderr, "Error setting dithering level: %s\n",
                    get_error_message(error).c_str());
    liq_result_destroy(res);
    liq_image_destroy(img);
    liq_attr_destroy(attr);
    return {};
  }

  // Remap image to indices
  std::vector<uint8_t> indices(width * height);
  error = liq_write_remapped_image(res, img, indices.data(), width * height);
  if (error != LIQ_OK) {
    fprintf(stderr, "Error during remapping: %s\n",
                    get_error_message(error).c_str());
    liq_result_destroy(res);
    liq_image_destroy(img);
    liq_attr_destroy(attr);
    return {};
  }

  const liq_palette *pal = liq_get_palette(res);

  // fprintf(stderr, "Palette after quantization (%u colors):\n", pal->count);

  std::vector<MzpImage::Color> palette_rgba;
  palette_rgba.reserve(pal->count);
  for (size_t i = 0; i < pal->count; ++i) {
    palette_rgba.emplace_back(
      pal->entries[i].r,
      pal->entries[i].g,
      pal->entries[i].b,
      pal->entries[i].a
    );
  }

  liq_result_destroy(res);
  liq_image_destroy(img);
  liq_attr_destroy(attr);

  return {indices, palette_rgba};
}

} // namespace mg::data
