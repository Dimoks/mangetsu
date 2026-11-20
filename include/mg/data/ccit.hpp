#pragma once

#include <string>

namespace mg::data {

// Convert text table to ccit binary format
bool txt_to_ccit(const std::string &src, std::string &dest);

// Convert ccit binary data to text table
bool ccit_to_txt(const std::string &src, std::string &dest);

} // namespace mg::data
