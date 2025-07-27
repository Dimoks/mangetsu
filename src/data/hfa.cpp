#include <string.h>

#include <mg/data/hfa.hpp>
#include <mg/util/endian.hpp>

namespace mg::data {

void Hfa::PackedEntryHeader::to_host_order() {
  size = mg::le_to_host_u32(size);
  offset = mg::le_to_host_u32(offset);
}

void Hfa::PackedEntryHeader::to_file_order() {
  size = mg::host_to_le_u32(size);
  offset = mg::host_to_le_u32(offset);
}

std::unique_ptr<MappedHfa>
MappedHfa::parse(std::shared_ptr<mg::fs::MappedFile> backing_data) {
  // Check file large enough to have a header
  if ((size_t)backing_data->size() < sizeof(Hfa::FileHeader)) {
    fprintf(stderr, "File too short to read header\n");
    return nullptr;
  }

  // Check magic is correct
  Hfa::FileHeader header = *(Hfa::FileHeader *)backing_data->data();
  if (!!memcmp(header.magic, Hfa::MAGIC, strlen(Hfa::MAGIC))) {
    fprintf(stderr, "File has invalid magic\n");
    return nullptr;
  }

  // Parse each of the header entries
  std::vector<Hfa::PackedEntryHeader> entries;
  const uint32_t entry_count = le_to_host_u32(header.entry_count);
  const Hfa::PackedEntryHeader *entry_ptr =
      reinterpret_cast<const Hfa::PackedEntryHeader *>(backing_data->data() +
                                                       sizeof(Hfa::FileHeader));
  for (uint32_t i = 0; i < entry_count; i++, entry_ptr++) {
    // Copy the entry, convert it to host order and add it to our entry list
    Hfa::PackedEntryHeader entry = *entry_ptr;
    entry.to_host_order();
    entries.emplace_back(entry);
  }

  return std::unique_ptr<MappedHfa>(new MappedHfa(backing_data, entries));
}

// HfaEntry implementation
HfaEntry::HfaEntry(std::shared_ptr<mg::fs::MappedFile> file, size_t offset,
                    size_t size, size_t index, const std::string &key,
                    const std::string &filename)
    : _file(file), _offset(offset), _size(size),
      _index(index), _key(key), _filename(filename) {}

size_t HfaEntry::seek(size_t offset, int whence) const {
  size_t new_pos = _seek_pos;
  if (whence == 0) {
    new_pos = offset;
  } else if (whence == 1) {
    new_pos += offset;
  } else if (whence == 2) {
    new_pos = _size + offset;
  } else {
    fprintf(stderr, "Invalid whence value: %d\n", whence);
    return _seek_pos;
  }
  if (new_pos > _size) {
    fprintf(stderr, "Seek beyond entry size.\n");
    return _seek_pos;
  }
  _seek_pos = new_pos;
  return _seek_pos;
}

std::string HfaEntry::read(size_t size) const {
  if (_seek_pos >= _size) {
    return "";
  }

  size_t read_size = size == static_cast<size_t>(-1) ? _size - _seek_pos :
                              std::min(size, _size - _seek_pos);

  if (!_data.empty()) {
    if (_seek_pos + read_size > _data.size()) {
      fprintf(stderr, "Invalid read range in cached data.\n");
      return "";
    }

    std::string result = _data.substr(_seek_pos, read_size);
    _seek_pos += read_size;
    return result;
  }

  const unsigned char *data = _file->data();
  size_t data_offset = _offset + _seek_pos;

  if (data_offset + read_size > static_cast<size_t>(_file->size())) {
    fprintf(stderr, "Invalid read range.\n");
    return "";
  }

  std::string result(data + data_offset, data + data_offset + read_size);
  _seek_pos += read_size;
  return result;
}

size_t HfaEntry::write(const std::string &data) {
  if (_data.empty()) {
    load_data();
  }

  size_t write_size = data.size();
  size_t new_pos = _seek_pos + write_size;

  if (new_pos > _data.size()) {
    _data.resize(new_pos);
    _size = new_pos;
  }

  _data.replace(_seek_pos, write_size, data);
  _seek_pos += write_size;
  return write_size;
}

size_t HfaEntry::truncate(size_t size) {
  if (size == static_cast<size_t>(-1)) {
    size = _seek_pos;
  }
  if (_data.empty()) {
    load_data();
  }
  if (size > _data.size()) {
    _data.resize(size, 0);
  } else {
    _data.resize(size);
  }
  _size = size;
  if (_seek_pos > _size) {
    _seek_pos = _size;
  }
  return _size;
}

bool HfaEntry::load_data() {
  _data = read(_size);
  return !_data.empty();
}

std::string HfaEntry::data() const {
  if (!_data.empty()) {
    // Return cached data if available
    return _data;
  }

  size_t pos = _seek_pos;
  seek(0);
  std::string result = read();
  seek(pos);
  return result;
}

bool HfaEntry::to_data(std::string &output) const {
  output = data();
  if (output.empty()) {
    fprintf(stderr, "No data to extract.\n");
    return false;
  }
  return true;
}

bool HfaEntry::from_data(const std::string &data) {
  _data = data;
  _size = data.size();
  _seek_pos = 0;
  return true;
}

// HfaArchive implementation
HfaArchive::HfaArchive(const std::string &path)
    : _path(path) {}

void HfaArchive::open(std::shared_ptr<mg::fs::MappedFile> file) {
  _file = file;
  if (!_file) {
    fprintf(stderr, "No file provided.\n");
    return;
  }

  if (static_cast<size_t>(_file->size()) < sizeof(Hfa::FileHeader)) {
    fprintf(stderr, "File too short to read header.\n");
    return;
  }

  const unsigned char *data = _file->data();
  Hfa::FileHeader header = *reinterpret_cast<const Hfa::FileHeader *>(data);
  // Check magic string "HUNEXGGEFA10"
  if (memcmp(header.magic, Hfa::MAGIC, strlen(Hfa::MAGIC)) != 0) {
    fprintf(stderr, "Invalid HFA magic.\n");
    return;
  }

  size_t header_size_bytes = sizeof(Hfa::FileHeader) +
      le_to_host_u32(header.entry_count) * sizeof(Hfa::PackedEntryHeader);
  size_t offset = sizeof(Hfa::FileHeader);

  for (uint32_t i = 0; i < le_to_host_u32(header.entry_count); i++) {
    if (offset + sizeof(Hfa::PackedEntryHeader) >
        static_cast<size_t>(_file->size())) {
      fprintf(stderr, "Invalid entry header offset.\n");
      return;
    }

    Hfa::PackedEntryHeader entry =
        *reinterpret_cast<const Hfa::PackedEntryHeader *>(data + offset);
    entry.to_host_order();
    std::string filename(entry.filename, strnlen(entry.filename, 96));

    // Handle duplicate filenames by appending (N) before extension
    std::string unique_filename = filename;
    size_t ext_pos = filename.rfind('.');
    std::string name_part = filename;
    std::string ext_part;

    if (ext_pos != std::string::npos) {
      name_part = filename.substr(0, ext_pos);
      ext_part = filename.substr(ext_pos);
    }

    int suffix = 2;

    while (_entries.find(unique_filename) != _entries.end()) {
      unique_filename = name_part + "(" + std::to_string(suffix) + ")" +
                        ext_part;
      suffix++;
    }

    // Calculate data offset: header_size_bytes + entry.offset
    size_t data_offset = header_size_bytes + entry.offset;
    size_t index = _entries.size();
    _entries.emplace(unique_filename, HfaEntry(_file, data_offset, entry.size,
                      index, unique_filename, filename));
    _entries_list.push_back(unique_filename);
    offset += sizeof(Hfa::PackedEntryHeader);
  }
}

bool hfa_cleared = false;

void HfaArchive::close() {
  if (!hfa_cleared) {
    _file.reset();
    _entries.clear();
    _entries_list.clear();
  }
}

// Add a new file entry to the archive at specified index
ssize_t HfaArchive::add_entry(const std::string &filename,
                              const std::string &data, ssize_t index) {
  // Check if filename is too long
  if (filename.size() > 95) {
    fprintf(stderr, "Filename too long: %s\n", filename.c_str());
    return -1;
  }

  // Handle duplicate filenames by appending (N) before extension
  std::string unique_filename = filename;
  size_t ext_pos = filename.rfind('.');
  std::string name_part = filename;
  std::string ext_part;

  if (ext_pos != std::string::npos) {
    name_part = filename.substr(0, ext_pos);
    ext_part = filename.substr(ext_pos);
  }

  int suffix = 2;
  while (_entries.find(unique_filename) != _entries.end()) {
    unique_filename = name_part + "(" + std::to_string(suffix) + ")" + ext_part;
    suffix++;
  }

  // Validate index
  size_t target_index = (index == -1) ? _entries_list.size() : index;
  if (index != -1 && static_cast<size_t>(index) > _entries_list.size()) {
    fprintf(stderr, "Index out of bounds: %zd\n", index);
    return -1;
  }

  // Create new entry with no backing file (data is in-memory)
  HfaEntry new_entry(nullptr, 0, data.size(), target_index, unique_filename,
                     filename);
  if (!new_entry.from_data(data)) {
    fprintf(stderr, "Failed to set data for entry: %s\n", filename.c_str());
    return -1;
  }

  // Add to entries and entries_list
  _entries.emplace(unique_filename, new_entry);
  _entries_list.insert(_entries_list.begin() + target_index, unique_filename);

  // Update indices of entries after the inserted one
  for (size_t i = target_index + 1; i < _entries_list.size(); ++i) {
    _entries.at(_entries_list[i]).set_index(i);
  }

  return target_index;
}

bool HfaArchive::hfa_write(std::string &output, size_t alignment,
                           uint8_t version, uint8_t align_byte) {
  Hfa::FileHeader header;
  // Copy magic string (12 bytes, no null terminator needed)
  memcpy(header.magic, Hfa::MAGIC, 12);
  header.entry_count = mg::host_to_le_u32(_entries.size());
  std::string header_data(reinterpret_cast<char *>(&header),
                          sizeof(Hfa::FileHeader));
  size_t offset = 0;
  std::vector<std::string> entry_headers;
  std::vector<std::string> entry_data;

  for (const auto &key : _entries_list) {
    auto &entry = _entries.at(key);
    std::string data = entry.data();

    if (data.empty()) {
      if (!entry.load_data()) {
        fprintf(stderr, "Failed to load entry data: %s\n",
                        entry.filename().c_str());
        return false;
      }
      data = entry.data();
    }

    Hfa::PackedEntryHeader entry_header = {};
    // Ensure filename fits in 96 bytes (95 chars + null terminator)
    std::string filename = entry.filename();

    if (filename.size() > 95) {
      fprintf(stderr, "Filename too long: %s\n", filename.c_str());
      filename = filename.substr(0, 95);
    }

    strncpy(entry_header.filename, filename.c_str(), 95);
    entry_header.filename[95] = '\0'; // Ensure null termination
    entry_header.offset = offset;
    entry_header.size = entry.size();
    entry_header.to_file_order();
    entry_headers.emplace_back(reinterpret_cast<char *>(&entry_header),
                                sizeof(Hfa::PackedEntryHeader));
    entry_data.push_back(data);
    offset += entry.size();

    if (alignment > 1) {
      size_t padding = alignment - (offset % alignment);
      if (padding < alignment) {
        offset += padding;
      }
    }
  }

  output = header_data;
  for (const auto &entry_header : entry_headers) {
    output += entry_header;
  }

  for (size_t i = 0; i < entry_data.size(); ++i) {
    size_t current_pos = output.size();
    size_t expected_pos = sizeof(Hfa::FileHeader) + entry_headers.size() *
                          sizeof(Hfa::PackedEntryHeader);
    if (alignment > 1) {
      expected_pos = (expected_pos + alignment - 1) / alignment * alignment;
    }
    if (current_pos < expected_pos) {
      output.append(expected_pos - current_pos, align_byte);
    }
    output += entry_data[i];
  }

  _file.reset();
  _entries.clear();
  _entries_list.clear();
  hfa_cleared = true;
  return true;
}

} // namespace mg::data
