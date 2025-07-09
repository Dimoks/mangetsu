#pragma once

#include <string>
#include <sstream>
#include <utility>

#include <mg/util/endian.hpp>

namespace mg::data {

// Make offset table from raw text content for MZP archive
std::pair<std::string, std::string> make_table(const std::string &content) {
  std::stringstream offset_table;
  uint32_t offset = 0;

  // Write initial offset (0)
  uint32_t initial_offset = host_to_be_u32(0);
  offset_table.write(reinterpret_cast<const char*>(&initial_offset),
                      sizeof(initial_offset));

  // Process content using std::string::find
  std::string::size_type start = 0;
  std::string::size_type pos = content.find('\n');
  while (pos != std::string::npos) {
    // Calculate offset to end of line (including newline)
    uint32_t next_offset = host_to_be_u32(offset + pos - start + 1);
    offset_table.write(reinterpret_cast<const char*>(&next_offset),
                        sizeof(next_offset));
    offset += pos - start + 1;
    start = pos + 1;
    pos = content.find('\n', start);
  }

  // Handle last line if no trailing newline
  if (start < content.size()) {
    uint32_t last_offset = host_to_be_u32(offset + content.size() - start);
    offset_table.write(reinterpret_cast<const char*>(&last_offset),
                        sizeof(last_offset));
    offset += content.size() - start;
  }

  // Write final offset and 0xFFFFFFFF
  uint32_t final_offset = host_to_be_u32(static_cast<uint32_t>(content.size()));
  offset_table.write(reinterpret_cast<const char*>(&final_offset),
                      sizeof(final_offset));
  uint32_t terminator = host_to_be_u32(0xFFFFFFFF);
  offset_table.write(reinterpret_cast<const char*>(&terminator),
                      sizeof(terminator));

  // Return offset table and original content as string table
  return {offset_table.str(), content};
}

} // namespace mg::data