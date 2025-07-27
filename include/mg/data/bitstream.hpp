// mg/data/bitstream.hpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace mg::data {

class BitStreamReader {
public:
  explicit BitStreamReader(const std::vector<uint8_t> &data,
                           bool msb_first = false);
  uint8_t readBit();
  uint32_t read(uint32_t nb_bits);
private:
  uint8_t readBit_lsb();
  uint8_t readBit_msb();
  uint32_t read_lsb(uint32_t nb_bits);
  uint32_t read_msb(uint32_t nb_bits);

  const std::vector<uint8_t> *_data;
  size_t _pos;
  uint32_t _curr_byte;
  int8_t _shift;
  bool _msb_first;
};

class BitStreamWriter {
public:
  explicit BitStreamWriter(std::shared_ptr<std::vector<uint8_t>> output,
                           bool msb_first = false);
  void writeBit(uint8_t bit);
  void write(uint32_t nb_bits, uint32_t value);
  void flush();
  std::shared_ptr<const std::vector<uint8_t>> data() const;
private:
  std::shared_ptr<std::vector<uint8_t>> _output_ptr;
  uint32_t _curr_byte;
  uint8_t _shift;
  bool _msb_first;
};

} // namespace mg::data
