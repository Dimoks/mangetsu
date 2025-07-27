#pragma once

#include <stdint.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <mg/util/fs.hpp>

namespace mg::data {

struct Hfa {

  // HUNEXGGEFA10 (magic, 12 bytes)
  // Header entry count (4 bytes)
  // Filename (32 * 3 bytes?)
  // Offset from end of header (4 bytes)
  // Size (4 bytes)
  // (padding 8 bytes?)
  // (padding 16 bytes?)

  static constexpr const char *MAGIC = "HUNEXGGEFA10";

  struct __attribute__((packed)) FileHeader {
    // Filename magic
    char magic[12];
    // Number of entries
    uint32_t entry_count;
  };

  struct __attribute__((packed)) PackedEntryHeader {
    // Name of the contained file
    char filename[96];
    // Start offset of the data (LE)
    uint32_t offset;
    // Data size bytes (LE)
    uint32_t size;
    uint8_t _padding[24];
    void to_host_order();
    void to_file_order();
  };
};

struct MappedHfa {
public:
  static std::unique_ptr<MappedHfa>
  parse(std::shared_ptr<mg::fs::MappedFile> backing_data);

  const std::vector<Hfa::PackedEntryHeader> &entries() const {
    return _entries;
  }

  const std::string_view entry_data(int index) const {
    const auto &entry = _entries.at(index);
    const size_t header_size_bytes =
        sizeof(Hfa::FileHeader) +
        sizeof(Hfa::PackedEntryHeader) * _entries.size();
    const size_t offset_bytes = header_size_bytes + entry.offset;
    return std::string_view(
        reinterpret_cast<const char *>(_backing_data->data()) + offset_bytes,
        entry.size);
  }

private:
  MappedHfa(std::shared_ptr<mg::fs::MappedFile> backing_data,
            std::vector<Hfa::PackedEntryHeader> entries)
      : _backing_data(backing_data), _entries(entries) {}

  std::shared_ptr<mg::fs::MappedFile> _backing_data;
  std::vector<Hfa::PackedEntryHeader> _entries;
};

// HFA entry
class HfaEntry {
public:
  HfaEntry(std::shared_ptr<mg::fs::MappedFile> file, size_t offset,
            size_t size, size_t index, const std::string &key = "",
            const std::string &filename = "");
  size_t seek(size_t offset, int whence = 0) const;
  std::string read(size_t size = -1) const;
  size_t write(const std::string &data);
  size_t truncate(size_t size = -1);
  bool load_data();
  std::string data() const;
  bool to_data(std::string &output) const;
  bool from_data(const std::string &data);
  std::string key() const { return _key; }
  std::string filename() const { return _filename; }
  size_t size() const { return _size; }
  size_t index() const { return _index; }
  void set_index(size_t index) { _index = index; }
  size_t tell() const { return _seek_pos; }

private:
  std::shared_ptr<mg::fs::MappedFile> _file;
  size_t _offset;
  size_t _size;
  size_t _index;
  mutable size_t _seek_pos = 0;
  std::string _key;
  std::string _filename;
  std::string _data;
};

// HFA archive parser
class HfaArchive {
public:
  HfaArchive(const std::string &path);
  ~HfaArchive() { close(); }
  void open(std::shared_ptr<mg::fs::MappedFile> file);
  void close();
  ssize_t add_entry(const std::string &filename,
                    const std::string &data, ssize_t index = -1);
  bool hfa_write(std::string &output, size_t alignment = 1,
                 uint8_t version = 1, uint8_t align_byte = 0x00);
  std::vector<std::reference_wrapper<HfaEntry>> entries() const {
    std::vector<std::reference_wrapper<HfaEntry>> result;
    result.reserve(_entries_list.size());
    for (const auto &key : _entries_list) {
      result.emplace_back(const_cast<HfaEntry&>(_entries.at(key)));
    }
    return result;
  }
  const std::vector<std::string> &entries_list() const {
    return _entries_list;
  }
  bool contains(const std::string &name) const {
    return _entries.find(name) != _entries.end();
  }
  bool contains(size_t index) const {
    return index < _entries_list.size();
  }
  HfaEntry &operator[](size_t index) {
    return _entries.at(_entries_list.at(index));
  }
  HfaEntry &operator[](const std::string &name) { return _entries.at(name); }
  size_t size() const { return _entries.size(); }

  class Iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = HfaEntry;
    using pointer = HfaEntry*;
    using reference = HfaEntry&;

    Iterator(std::vector<std::string>::const_iterator it,
             std::unordered_map<std::string, HfaEntry> &entries)
        : _it(it), _entries(entries) {}

    reference operator*() { return _entries.at(*_it); }
    pointer operator->() { return &(_entries.at(*_it)); }
    Iterator &operator++() { ++_it; return *this; }
    Iterator operator++(int) { Iterator tmp = *this; ++_it; return tmp; }
    bool operator==(const Iterator &other) const { return _it == other._it; }
    bool operator!=(const Iterator &other) const { return _it != other._it; }

  private:
    std::vector<std::string>::const_iterator _it;
    std::unordered_map<std::string, HfaEntry> &_entries;
  };

  Iterator begin() { return Iterator(_entries_list.begin(), _entries); }
  Iterator end() { return Iterator(_entries_list.end(), _entries); }

private:
  std::string _path;
  std::shared_ptr<mg::fs::MappedFile> _file;
  std::unordered_map<std::string, HfaEntry> _entries;
  std::vector<std::string> _entries_list;
};

} // namespace mg::data

