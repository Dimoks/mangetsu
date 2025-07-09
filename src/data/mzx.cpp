#include <cassert>
#include <vector>
#include <tuple>
#include <string.h>

#include <mg.hpp>
#include <mg/data/mzx.hpp>

namespace mg::data {

static const uint8_t CMD_RLE = 0;
static const uint8_t CMD_BACKREF = 1;
static const uint8_t CMD_RINGBUF = 2;
static const uint8_t CMD_LITERAL = 3;

class RingBuffer {
public:
  static const size_t BUFFER_SIZE = 64;

  RingBuffer() : write_offset(0), read_offset(0) {
    buffer.resize(BUFFER_SIZE);
    flags.resize(BUFFER_SIZE, false);
    memset(buffer.data(), 0, BUFFER_SIZE * sizeof(uint16_t));
  }

  void write(uint16_t value) {
    buffer[write_offset] = value;
    flags[write_offset] = true;
    write_offset = (write_offset + 1) % BUFFER_SIZE;
  }

  int read(size_t index) const {
    size_t actual_index = (read_offset + index) % BUFFER_SIZE;
    if (index >= BUFFER_SIZE || !flags[actual_index]) {
      return -1;
    }
    return buffer[actual_index];
  }

  int find_word(uint16_t word) const {
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
      if (read(i) == word) {
        return static_cast<int>(i);
      }
    }

    return -1;
  }

  size_t size() const {
    return BUFFER_SIZE;
  }

private:
  std::vector<uint16_t> buffer;
  std::vector<bool> flags;
  size_t write_offset;
  size_t read_offset;
};

bool mzx_compress(const std::string &raw, std::string &out,
                  int level, bool invert) {
  MzxHeader header;
  memcpy(header.magic, MzxHeader::FILE_MAGIC, sizeof(header.magic));
  header.decompressed_size = raw.size();

  // Estimate our final size
  // Write header
  header.to_file_order();
  std::vector<uint8_t> out_buffer;
  out_buffer.insert(out_buffer.end(), reinterpret_cast<uint8_t*>(&header),
                    reinterpret_cast<uint8_t*>(&header) + sizeof(header));

  if (level == 0) {
    for (std::string::size_type pos = 0; pos < raw.size();) {
      // Len field is 6 bits, each word is 2 bytes, we can write 128 bytes per
      // literal record
      const std::string::size_type bytes_remaining = raw.size() - pos;
      const unsigned bytes_to_write = bytes_remaining < 128
                                      ? bytes_remaining : 128;

      // Convert that to a number of words to write.
      // If we have a trailing byte, that needs an extra word.
      uint8_t words_to_write = bytes_to_write / 2;
      if (bytes_to_write & 1) {
        words_to_write++;
      }

      // The number of words to write offset by one in the literal
      words_to_write--;

      // Pack the cmd
      uint8_t cmd = CMD_LITERAL | (words_to_write << 2);
      out_buffer.push_back(cmd);

      // Write the next bytes_to_write datums
      for (unsigned i = 0; i < bytes_to_write; i++) {
        uint8_t byte = static_cast<uint8_t>(raw[pos + i]);
        if (invert) {
          byte ^= 0xFF;
        }
        out_buffer.push_back(byte);
      }

      // Increment pos
      pos += bytes_to_write;
    }
  } else {
    size_t cursor = 0;
    uint8_t cmd;
    int clear_count = 0x1000;
    int lit_start = 0;
    int lit_len = 0;
    int best_type = 0; // 0: LITERAL, 1: RLE, 2: BACKREF, 3: RINGBUF
    int best_len = 0;
    int rle_len = 0;
    int br_len = 0;
    int br_dist = 0;
    int rb_len = 0;
    int rb_index = -1;
    RingBuffer ring_buffer;

    std::vector<uint8_t> raw_data(raw.begin(), raw.end());

    if (raw_data.size() % 2 == 1) {
      raw_data.push_back(0x00);
    }

    std::vector<uint16_t> words(raw_data.size() / 2);

    for (size_t i = 0; i < raw_data.size(); i += 2) {
      words[i / 2] = static_cast<uint16_t>(raw_data[i]) |
                      (static_cast<uint16_t>(raw_data[i + 1]) << 8);
    }

    auto literal_compress = [&](size_t lit_start, size_t lit_len) {
      assert(lit_len <= 64);
      cmd = CMD_LITERAL | ((lit_len - 1) << 2);
      out_buffer.push_back(cmd);

      if (lit_start + lit_len > words.size()) {
        fprintf(stderr, "Out of bounds array words (lit_start=%zu, "
                        "lit_len=%zu, size=%zu)\n", lit_start,
                        lit_len, words.size());
        return;
      }

      for (size_t i = 0; i < lit_len; i++) {
        uint16_t word = words[lit_start + i];
        word ^= (invert * 0xFFFF);
        out_buffer.push_back(static_cast<uint8_t>(word & 0xFF));
        out_buffer.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
      }
    };

    auto rle_compress_length = [&]() -> size_t {
      uint16_t last = (clear_count <= 0 || clear_count == 0x1000) ?
                      (invert ? 0xFFFF : 0x0000) : words[cursor - 1];
      if (cursor >= words.size() || words[cursor] != last) return 0;

      size_t length = 1;
      size_t max_len = std::min(64, clear_count);
      while (length < max_len && cursor + length < words.size()) {
        if (words[cursor + length] != last) break;
        length++;
      }
      return length;
    };

    auto backref_compress_length = [&]() -> std::pair<size_t, size_t> {
      if (cursor == 0 || cursor >= words.size()) return {0, 0};

      size_t search_start = cursor > 255 ? cursor - 255 : 0;
      size_t max_len = std::min(64, clear_count);
      size_t max_dist = 0x1000 - clear_count;
      size_t best_len = 0;
      size_t best_dist = 0;
      for (size_t i = cursor; i > search_start; i--) {
        if (cursor - (i - 1) > max_dist) break;
        if (words[i - 1] != words[cursor]) continue;

        size_t length = 0;
        while (length < max_len && (cursor + length) < words.size() &&
                words[i - 1 + length] == words[cursor + length]){
          ++length;
        }
        if (length > best_len) {
          best_len = length;
          best_dist = cursor - (i - 1);
          if (best_len == max_len) break;
        }
      }
      return {best_dist, best_len};
    };

    while (cursor < words.size()) {
      best_len = 0;
      best_type = 0; // LITERAL
      uint16_t current_word = words[cursor];
      current_word ^= (invert * 0xFFFF);
      rle_len = rle_compress_length();
      if (level >= 2) {
        if (cursor > 0) {
          std::tie(br_dist, br_len) = backref_compress_length();
          rb_index = ring_buffer.find_word(current_word);
          rb_len = (rb_index != -1) ? 1 : 0;
        }
      }

      if (rle_len > 0) {
        best_len = rle_len;
        best_type = 1; // RLE
      }

      if (level >= 2 && cursor > 0) {
        if (br_len > best_len + 1 &&
            !(best_len > 0 && best_len * 2 + 1 >= br_len &&
              0x1000 - clear_count + best_len >= br_dist)) {
          best_len = br_len;
          best_type = 2; // BACKREF
        }
        if (rb_len > 0 && rb_len > best_len) {
          best_len = rb_len;
          best_type = 3; // RINGBUF
        }
      }

      if (best_type == 0) { // LITERAL
        switch (lit_len) {
          case 0:
            lit_start = cursor;
            lit_len = 1;
            break;
          case 63:
            literal_compress(lit_start, 64);
            if (clear_count <= 0) {
              clear_count = 0x1000;
            }
            lit_len = 0;
            break;
          default:
            lit_len++;
        }

        if (level >= 2) {
          ring_buffer.write(current_word);
        }
        cursor++;
        clear_count--;

        if (clear_count <= 0 && lit_len > 0) {
          literal_compress(lit_start, lit_len);
          clear_count = 0x1000;
          lit_len = 0;
        }

      } else {
        if (lit_len > 0) {
          literal_compress(lit_start, lit_len);
          if (clear_count <= 0) {
            clear_count = 0x1000;
          }
          lit_len = 0;
        }
        if (best_type == 1) { // RLE
          cmd = CMD_RLE | ((rle_len - 1) << 2);
          out_buffer.push_back(cmd);
        } else if (best_type == 2) { // BACKREF
          cmd = CMD_BACKREF | ((br_len - 1) << 2);
          out_buffer.push_back(cmd);
          out_buffer.push_back(static_cast<uint8_t>(br_dist - 1));
        } else if (best_type == 3) { // RINGBUF
          cmd = CMD_RINGBUF | (rb_index << 2);
          out_buffer.push_back(cmd);
        }
        clear_count -= best_len;
        if (clear_count <= 0) {
          clear_count = 0x1000;
        }
        cursor += best_len;
      }
    }

    if (lit_len > 0) {
      literal_compress(lit_start, lit_len);
    }
  }
  out.resize(out_buffer.size());
  memcpy(out.data(), out_buffer.data(), out_buffer.size());

  return true;
}

bool mzx_decompress(const std::string &compressed, std::string &out,
                    bool invert) {
  // If header is too small, bail immediately
  if (compressed.size() < sizeof(MzxHeader)) {
    fprintf(stderr, "Header too small\n");
    return false;
  }

  // Pun start of data stream into header
  MzxHeader header = *reinterpret_cast<const MzxHeader *>(compressed.data());
  header.to_host_order();

  // If the magic doesn't match, do not try and uncompress
  if (memcmp(header.magic, MzxHeader::FILE_MAGIC, sizeof(header.magic)) != 0) {
    fprintf(stderr, "Invalid file magic\n");
    return false;
  }

  // Resize output buffer to accomodate decompressed data
  out.resize(header.decompressed_size);

  // Last written short
  uint8_t last[2];
  memset(last, invert ? 0xFF : 0x00, sizeof(last));

  // Ring buffer
  uint16_t ring_buffer[64];
  memset(ring_buffer, invert ? 0xFF : 0x00, sizeof(ring_buffer));

  // Clear counter. Last data is reinitialized on zero.
  int clear_count = 0;

  // Start reading right after the header
  std::string::size_type read_offset = sizeof(MzxHeader);
  std::string::size_type decompress_offset = 0;
  unsigned ring_buffer_write_offset = 0;
  while (read_offset < compressed.size()) {
    // Get type / len
    const uint8_t len_cmd = compressed[read_offset++];
    const unsigned cmd = len_cmd & 0b11;
    const unsigned len = len_cmd >> 2;

    // Reset counter
    if (clear_count <= 0) {
      clear_count = 0x1000;
      memset(last, invert ? 0xFF : 0x00, sizeof(last));
    }

    auto emit_byte = [&](uint8_t byte) {
      if (decompress_offset >= out.size()) {
        // fprintf(stderr, "Tried to write %d bytes past end of buffer: %02x\n",
        //         (int)decompress_offset - (int)out.size(), byte);
        return;
      }
      out[decompress_offset++] = byte;
    };

    switch (cmd) {

    case CMD_RLE: {
      // Repeat last two bytes len + 1 times
      for (unsigned i = 0; i <= len; i++) {
        emit_byte(last[0]);
        emit_byte(last[1]);
      }
    } break;

    case CMD_BACKREF: {
      const int lookback_distance =
          2 * (static_cast<uint8_t>(compressed[read_offset++]) + 1);
      for (unsigned i = 0; i <= len; i++) {
        const std::string::size_type lookback_offset =
            decompress_offset - lookback_distance;

        // Read 2 bytes into last buffer
        last[0] = out[lookback_offset];
        last[1] = out[lookback_offset + 1];

        // Write those bytes to end of stream
        emit_byte(last[0]);
        emit_byte(last[1]);
      }
    } break;

    case CMD_RINGBUF: {
      // Load ring buffer data at position len into last
      *reinterpret_cast<uint16_t *>(last) = ring_buffer[len];

      // Emit last
      emit_byte(last[0]);
      emit_byte(last[1]);
    } break;

    case CMD_LITERAL: {
      for (unsigned i = 0; i <= len; i++) {
        const uint8_t r0 = compressed[read_offset++] ^ (invert ? 0xFF : 0x00);
        const uint8_t r1 = compressed[read_offset++] ^ (invert ? 0xFF : 0x00);

        // Update last
        last[0] = r0;
        last[1] = r1;

        // Emit data
        emit_byte(last[0]);
        emit_byte(last[1]);

        // Write to ring buffer
        ring_buffer[ring_buffer_write_offset++] = (r1 << 8) | r0;
        ring_buffer_write_offset &= 0x3f;
      }
      break;
    }
    }
    clear_count -= (cmd == CMD_RINGBUF) ? 1 : (len + 1);
  }

  return true;
}

} // namespace mg::data
