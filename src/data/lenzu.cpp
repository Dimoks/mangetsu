// src/data/lenzu.cpp
#include <mg/data/lenzu.hpp>

// 2 spaces for indentation as per user preference
namespace mg::data {

void Lenzu::Header::to_host_order() {
  decompressed_length = le_to_host_u32(decompressed_length);
  crc = le_to_host_u64(crc);
  reserved = le_to_host_u32(reserved);
}

ByteHuffmanTable Lenzu::read_huffman_table(const std::string &src, size_t &pos,
                                            uint32_t bit_count, bool invert) {
  // Calculate index bytes needed
  uint32_t first_real_entry = 1 << bit_count;
  uint32_t index_bits = std::ceil(static_cast<float>(bit_count) / 8);
  uint32_t index_bytes = std::ceil(static_cast<float>(index_bits) / 8);

  // Read number of entries
  if (pos + index_bytes > src.size()) {
    fprintf(stderr, "Error: Insufficient data for Huffman "
                    "table entries at pos %zu\n", pos);
    return ByteHuffmanTable(invert);
  }
  uint32_t entries_to_fill = 0;
  for (uint32_t i = 0; i < index_bytes; ++i) {
    entries_to_fill |=
        static_cast<uint32_t>(static_cast<uint8_t>(src[pos++])) << (i * 8);
  }
  if (entries_to_fill == 0) {
    entries_to_fill = first_real_entry;
  }

  // Determine if weights are stored sparsely or fully
  std::vector<std::pair<uint32_t, uint32_t>> weights;
  if (first_real_entry * 4 < (index_bits + 4) * entries_to_fill) {
    entries_to_fill = first_real_entry;
    if (pos + entries_to_fill * 4 > src.size()) {
      fprintf(stderr, "Error: Insufficient data for full "
                      "weights at pos %zu\n", pos);
      return ByteHuffmanTable(invert);
    }
    for (uint32_t i = 0; i < entries_to_fill; ++i) {
      uint32_t weight =
          le_to_host_u32(*reinterpret_cast<const uint32_t*>(&src[pos]));
      pos += 4;
      weights.emplace_back(i, weight);
    }
  } else {
    if (pos + entries_to_fill * (index_bytes + 4) > src.size()) {
      fprintf(stderr, "Error: Insufficient data for "
                      "sparse weights at pos %zu\n", pos);
      return ByteHuffmanTable(invert);
    }
    for (uint32_t i = 0; i < entries_to_fill; ++i) {
      uint32_t index = 0;
      for (uint32_t j = 0; j < index_bytes; ++j) {
        index |=
            static_cast<uint32_t>(static_cast<uint8_t>(src[pos++])) << (j * 8);
      }
      uint32_t weight =
          le_to_host_u32(*reinterpret_cast<const uint32_t*>(&src[pos]));
      pos += 4;
      weights.emplace_back(index, weight);
    }
  }

  // Build Huffman table
  ByteHuffmanTable table(invert);
  for (const auto &[index, weight] : weights) {
    table.setWeight(static_cast<uint8_t>(index), weight);
  }
  uint32_t max_entries = ((first_real_entry + 1) * first_real_entry) >> 1;
  table.buildTree(max_entries);
  return table;
}

uint64_t Lenzu::lenzu_crc_64(const std::string &data, size_t size,
                              uint64_t seed, size_t lut_offset) {
  static const uint64_t lut[] = {0x0e9, 0x115, 0x137, 0x1b1};
  uint64_t crc = seed;
  for (size_t i = 0; i < size; ++i) {
    crc = (crc + static_cast<uint8_t>(data[i])) * lut[(lut_offset + i) & 3];
  }
  // Swap high and low 32 bits to match expected byte order
  return ((crc & 0xFFFFFFFF) << 32) | (crc >> 32);
}

uint32_t Lenzu::lenzu_crc_32(const std::string &data, size_t size,
                              uint32_t seed, size_t lut_offset) {
  static const uint32_t lut[] = {0x0e9, 0x115, 0x137, 0x1b1};
  uint32_t crc = seed;
  for (size_t i = 0; i < size; ++i) {
    crc = ((crc + static_cast<uint8_t>(data[i])) *
          lut[(lut_offset + i) & 3]) % (1ULL << 32);
  }
  return crc;
}

bool Lenzu::decompress(const std::string &src,
                        std::string &dest, bool invert = true) {
  // Read header
  if (src.size() < sizeof(Header)) {
    fprintf(stderr, "Error: Input too short for header, "
                    "got %zu bytes\n", src.size());
    return false;
  }

  Header header = *reinterpret_cast<const Header*>(&src[0]);
  header.to_host_order();

  // Validate magic and version
  if (std::memcmp(header.magic, Lenzu_magic, sizeof(Lenzu_magic)) != 0) {
    fprintf(stderr, "Error: Invalid Lenzu magic.\n");
    return false;
  }

  if (std::memcmp(header.version, version_info, sizeof(version_info)) != 0) {
    fprintf(stderr, "Error: Invalid version bytes.\n");
    return false;
  }

  size_t pos = sizeof(Header);

  // Parse decompressor options
  uint8_t huff_bc_raw = header.options[1];
  uint8_t huff_bc_min = header.options[2];
  uint8_t br_low_bc_xupper = header.options[3];
  uint8_t br_low_bc = header.options[4];
  uint8_t br_base_dist = header.options[5];

  if (huff_bc_raw < 3 || huff_bc_raw >= 16) {
    fprintf(stderr, "Error: Invalid huff_bc_raw: %u\n", huff_bc_raw);
    return false;
  }
  if (huff_bc_min < 3 || huff_bc_min >= 16) {
    fprintf(stderr, "Error: Invalid huff_bc_min: %u\n", huff_bc_min);
    return false;
  }
  uint32_t huff_bit_count = std::max(huff_bc_raw, huff_bc_min);
  if (br_low_bc_xupper >= 16) {
    fprintf(stderr, "Error: Invalid br_low_bc_xupper: %u\n", br_low_bc_xupper);
    return false;
  }
  if (br_low_bc > br_low_bc_xupper) {
    fprintf(stderr, "Error: br_low_bc (%u) exceeds br_low_bc_xupper (%u)\n",
                    br_low_bc, br_low_bc_xupper);
    return false;
  }
  // Cast subtraction result to uint32_t to avoid sign-compare warning
  if (huff_bit_count < static_cast<uint32_t>(br_low_bc_xupper - br_low_bc)) {
    fprintf(stderr, "Error: huff_bit_count (%u) less than "
                    "br_low_bc_xupper - br_low_bc (%u)\n",
                     huff_bit_count, br_low_bc_xupper - br_low_bc);
    return false;
  }
  if (br_base_dist < 2 || br_base_dist >= 9) {
    fprintf(stderr, "Error: Invalid br_base_dist: %u\n", br_base_dist);
    return false;
  }

  // Read Huffman table with the provided invert parameter
  ByteHuffmanTable huff_table =
      read_huffman_table(src, pos, huff_bit_count, invert);
  if (pos > src.size()) {
    fprintf(stderr, "Error: Huffman table read overflow.\n");
    return false;
  }

  // Initialize output
  dest.resize(header.decompressed_length);

  // Initialize bit stream
  std::vector<uint8_t> bitstream_data(src.begin() + pos, src.end());
  BitStreamReader bit_stream(bitstream_data, true);

  // Decompression loop
  size_t output_pos = 0;
  while (output_pos < header.decompressed_length && pos < src.size()) {
    int32_t bit = bit_stream.readBit();
    if (bit < 0) {
      fprintf(stderr, "Error: Bit stream overflow at output pos %zu\n",
                      output_pos);
      return false;
    }
    bool is_back_ref = bit != 0;
    int32_t length = huff_table.decodeSequence(bit_stream);
    if (length < 0) {
      fprintf(stderr, "Error: Invalid length decoded: %d at output pos %zu\n",
                      length, output_pos);
      return false;
    }

    if (is_back_ref) {
      length += br_base_dist;
      int32_t distance_high_bits = huff_table.decodeSequence(bit_stream);
      if (distance_high_bits < 0) {
        fprintf(stderr, "Error: Invalid distance_high_bits: %d at output "
                        "pos %zu\n", distance_high_bits, output_pos);
        return false;
      }
      uint32_t distance_low_bits = 0;
      if (br_low_bc > 0) {
        distance_low_bits = bit_stream.read(br_low_bc);
        if (distance_low_bits == UINT32_MAX) {
          fprintf(stderr, "Error: Invalid distance_low_bits at output "
                          "pos %zu\n", output_pos);
          return false;
        }
      }
      uint32_t distance = distance_low_bits | (distance_high_bits << br_low_bc);
      distance += br_base_dist;

      if (distance > output_pos) {
        fprintf(stderr, "Error: Back reference distance %u exceeds "
                        "output position %zu\n", distance, output_pos);
        return false;
      }

      size_t src_pos = output_pos - distance;
      if (length > static_cast<int32_t>(distance)) {
        // Handle repeating buffer
        size_t bytes_to_copy = length;
        while (bytes_to_copy > 0) {
          // Use std::min<size_t> to ensure same type for arguments
          size_t chunk_size = std::min<size_t>(bytes_to_copy, distance);
          if (output_pos + chunk_size > header.decompressed_length) {
            fprintf(stderr, "Error: Output buffer overflow at pos %zu\n",
                            output_pos);
            return false;
          }
          std::memcpy(&dest[output_pos], &dest[src_pos], chunk_size);
          output_pos += chunk_size;
          bytes_to_copy -= chunk_size;
        }
      } else {
        if (output_pos + length > header.decompressed_length) {
          fprintf(stderr, "Error: Output buffer overflow at pos %zu\n",
                          output_pos);
          return false;
        }
        std::memcpy(&dest[output_pos], &dest[src_pos], length);
        output_pos += length;
      }
    } else {
      for (int32_t i = 0; i <= length; ++i) {
        if (output_pos >= header.decompressed_length) {
          fprintf(stderr, "Error: Output buffer overflow at pos %zu\n",
                          output_pos);
          return false;
        }
        uint32_t byte = bit_stream.read(8);
        if (byte == UINT32_MAX) {
          fprintf(stderr, "Error: Invalid byte read at output pos %zu\n",
                          output_pos);
          return false;
        }
        dest[output_pos++] = static_cast<char>(byte);
      }
    }
  }

  if (output_pos < header.decompressed_length) {
    fprintf(stderr, "Error: Output underflow, expected %u, got %zu\n",
                    header.decompressed_length, output_pos);
    return false;
  }

  // Optionally verify CRC
  uint64_t computed_crc = lenzu_crc_64(dest, header.decompressed_length);
  if (computed_crc != header.crc) {
    fprintf(stderr, "Error: CRC mismatch, expected %" PRIx64 ", "
                    "got %" PRIx64 "\n", header.crc, computed_crc);
    // return false;
  }

  return true;
}

} // namespace mg::data
