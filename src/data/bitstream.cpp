// mg/data/bitstream.cpp
#include <mg/data/bitstream.hpp>
#include <cstdio>

namespace mg::data {

// ─────────────────────────────────────────────────────────────────────────────
// BitStreamReader
// ─────────────────────────────────────────────────────────────────────────────

BitStreamReader::BitStreamReader(const std::vector<uint8_t> &data,
                                 bool msb_first)
    : _data(&data),
      _pos(0),
      _curr_byte(0),
      _shift(msb_first ? -1 : 8),
      _msb_first(msb_first) {}

uint8_t BitStreamReader::readBit() {
  if (_msb_first) {
    return readBit_msb();
  } else {
    return readBit_lsb();
  }
}

uint32_t BitStreamReader::read(uint32_t nb_bits) {
  if (_msb_first) {
    return read_msb(nb_bits);
  } else {
    return read_lsb(nb_bits);
  }
}

uint8_t BitStreamReader::readBit_lsb() {
  if (_shift == 8) {
    if (_pos >= _data->size()) {
      fprintf(stderr, "Error: Bit stream overflow at pos %zu\n", _pos);
      return 0;
    }
    _curr_byte = (*_data)[_pos++];
    _shift = 1;
    return _curr_byte & 1;
  } else {
    uint8_t val = (_curr_byte >> _shift) & 1;
    _shift++;
    return val;
  }
}

uint8_t BitStreamReader::readBit_msb() {
  if (_shift == -1) {
    if (_pos >= _data->size()) {
      fprintf(stderr, "Error: Bit stream overflow at pos %zu\n", _pos);
      return 0;
    }
    _curr_byte = (*_data)[_pos++];
    _shift = 6;
    return (_curr_byte >> 7) & 1;
  } else {
    uint8_t val = (_curr_byte >> _shift) & 1;
    _shift--;
    return val;
  }
}

uint32_t BitStreamReader::read_lsb(uint32_t nb_bits) {
  if (nb_bits == 0) return 0;
  // Calculate number of new bytes needed
  size_t nb_new_bytes = (_shift + nb_bits - 1) / 8;
  if (nb_new_bytes > 0) {
    for (size_t i = 0; i < nb_new_bytes; ++i) {
      if (_pos >= _data->size()) {
        fprintf(stderr, "Error: Bit stream overflow at pos %zu\n", _pos);
        return 0;
      }
      _curr_byte |= static_cast<uint32_t>((*_data)[_pos++]) << 8;
    }
  }
  uint32_t val = (_curr_byte >> _shift) & ((1u << nb_bits) - 1);
  _shift += nb_bits;
  if (nb_new_bytes > 0) {
    _curr_byte >>= (nb_new_bytes * 8);
    _shift -= (nb_new_bytes * 8);
  }
  return val;
}

uint32_t BitStreamReader::read_msb(uint32_t nb_bits) {
  if (nb_bits == 0) return 0;
  // Calculate number of new bytes needed
  size_t nb_new_bytes = (nb_bits + (6 - _shift)) / 8;
  if (nb_new_bytes > 0) {
    uint32_t new_data = 0;
    for (size_t i = 0; i < nb_new_bytes; ++i) {
      if (_pos >= _data->size()) {
        fprintf(stderr, "Error: Bit stream overflow at pos %zu\n", _pos);
        return 0;
      }
      new_data = (new_data << 8) | (*_data)[_pos++];
    }
    _curr_byte = (_curr_byte & ((1u << (_shift + 1)) - 1)) <<
                 (nb_new_bytes * 8) | new_data;
  } else {
    _curr_byte &= ((1u << (_shift + 1)) - 1);
  }
  _shift = (_shift - nb_bits) % 8;
  if (_shift == 7) {
    _shift = -1;
  }
  uint32_t val = _curr_byte >> (_shift + 1);
  _curr_byte &= 0xFF;
  return val;
}

// ─────────────────────────────────────────────────────────────────────────────
// BitStreamWriter
// ─────────────────────────────────────────────────────────────────────────────

BitStreamWriter::BitStreamWriter(std::shared_ptr<std::vector<uint8_t>> output,
                                 bool msb_first)
    : _output_ptr(std::move(output)),
      _curr_byte(0),
      _shift(0),
      _msb_first(msb_first) {
  if (!_output_ptr) {
    _output_ptr = std::make_shared<std::vector<uint8_t>>();
  }
}

void BitStreamWriter::writeBit(uint8_t bit) {
  // TODO: MSB-first case
  _curr_byte |= (bit & 1) << _shift;
  if (_shift == 7) {
    _output_ptr->push_back(_curr_byte);
    _shift = 0;
    _curr_byte = 0;
  } else {
    _shift++;
  }
}

void BitStreamWriter::write(uint32_t nb_bits, uint32_t value) {
  // TODO: MSB-first case
  _curr_byte |= (value & ((1u << nb_bits) - 1)) << _shift;
  _shift += nb_bits;
  if (_shift >= 8) {
    size_t nb_bytes = _shift / 8;
    size_t written_bits = nb_bytes * 8;
    for (size_t i = 0; i < nb_bytes; ++i) {
      _output_ptr->push_back(_curr_byte & 0xFF);
      _curr_byte >>= 8;
    }
    _shift -= written_bits;
  }
}

void BitStreamWriter::flush() {
  if (_shift != 0) {
    _output_ptr->push_back(_curr_byte);
    _shift = 0;
    _curr_byte = 0;
  }
}

std::shared_ptr<const std::vector<uint8_t>> BitStreamWriter::data() const {
  return _output_ptr;
}

} // namespace mg::data
