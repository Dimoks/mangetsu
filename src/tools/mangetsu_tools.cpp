#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <regex>

#include <mg.hpp>
#include <mg/data/hfa.hpp>
#include <mg/data/ccit.hpp>
#include <mg/data/lenzu.hpp>
#include <mg/data/cbg.hpp>
#include <mg/data/mzp.hpp>
#include <mg/data/mzpimage.hpp>
#include <mg/data/mzp_table_maker.hpp>
#include <mg/data/mzx.hpp>
#include <mg/data/mrg.hpp>
#include <mg/data/nam.hpp>
#include <mg/data/nxx.hpp>
#include <mg/util/fs.hpp>
#include <mg/util/string.hpp>

namespace fs = std::filesystem;

const std::vector<std::string> KNOWN_EXTENSIONS =
    {".cbg", ".mzp", ".ccit", ".chs", ".ctd", ".mp4", ".hw"};

bool as_is = false;
bool mzx_invert = false;
bool mrg_csv = false;
std::set<long> indexes;

struct NaturalCompare {
  bool operator()(const std::string &a, const std::string &b) const {
    // Extract filenames from paths.
    std::string filename_a = fs::path(a).filename().string();
    std::string filename_b = fs::path(b).filename().string();

    // Find first dot (after prefix) in filenames.
    size_t dot_a = filename_a.find('.');
    size_t dot_b = filename_b.find('.');

    if (dot_a == std::string::npos || dot_b == std::string::npos) {
      fprintf(stderr, "Invalid filename format: %s or %s\n",
                      filename_a.c_str(), filename_b.c_str());
      return filename_a < filename_b; // Fallback to lexicographical comparison.
    }

    // Extract prefixes.
    std::string prefix_a = filename_a.substr(0, dot_a);
    std::string prefix_b = filename_b.substr(0, dot_b);

    if (prefix_a != prefix_b) {
      return prefix_a < prefix_b;
    }

    // Find the end of the number part (next dot or end of string).
    size_t next_dot_a = filename_a.find('.', dot_a + 1);
    size_t next_dot_b = filename_b.find('.', dot_b + 1);

    // Extract number parts.
    std::string num_a = (next_dot_a == std::string::npos) ?
                        filename_a.substr(dot_a + 1) :
                        filename_a.substr(dot_a + 1, next_dot_a - dot_a - 1);
    std::string num_b = (next_dot_b == std::string::npos) ?
                        filename_b.substr(dot_b + 1) :
                        filename_b.substr(dot_b + 1, next_dot_b - dot_b - 1);

    // Check that number parts contain only digits.
    for (char c : num_a) {
      if (!std::isdigit(c)) {
        fprintf(stderr, "Number part '%s' in '%s' contains non-digits.\n",
                        num_a.c_str(), filename_a.c_str());
        return filename_a < filename_b; // Fallback.
      }
    }
    for (char c : num_b) {
      if (!std::isdigit(c)) {
        fprintf(stderr, "Number part '%s' in '%s' contains non-digits.\n",
                        num_b.c_str(), filename_b.c_str());
        return filename_a < filename_b; // Fallback.
      }
    }

    // Convert to integers for comparison.
    try {
      long val_a = std::stol(num_a);
      long val_b = std::stol(num_b);
      return val_a < val_b;
    } catch (const std::exception &e) {
      fprintf(stderr, "Error converting number: %s\n", e.what());
      return filename_a < filename_b; // Fallback.
    }
  }
};

std::string toLower(const std::string &str) {
  std::string lowerStr = str;
  std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                  [](unsigned char c) {
    return std::tolower(c);
  });
  return lowerStr;
}

// Check if string ends with suffix (C++17 compatible).
bool ends_with(const std::string &str, const std::string &suffix) {

  return str.size() >= suffix.size() && toLower(str).compare(str.size() -
          suffix.size(), suffix.size(), suffix) == 0;
}

// Convert glob pattern to regex pattern.
std::string glob_to_regex(const std::string &glob_pattern) {

  std::string regex_pattern;

  for (char ch : glob_pattern) {
    switch (ch) {
      case '*':
        regex_pattern += "[^/\\\\]*";
        break;
      case '?':
        regex_pattern += '.';
        break;
      case '.':
        regex_pattern += "\\.";
        break;
      case '+':
      case '(':
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
        regex_pattern += '\\';
        regex_pattern += ch;
        break;
      default:
        regex_pattern += ch;
        break;
    }
  }
  return regex_pattern;
}

// Check if pattern contains glob characters (* or ?).
bool is_glob_pattern(const std::string &pattern) {

  return pattern.find('*') != std::string::npos ||
         pattern.find('?') != std::string::npos;
}

// Check if path is a directory or potential directory.
bool isPotentialDirectory(const std::string &path) {

  if (is_glob_pattern(path)) return false;

  if (fs::is_directory(path)) return true;

  size_t dotIndex = path.rfind('.');
  if (dotIndex == std::string::npos) return true;

  if (dotIndex + 1 < path.length()) {
    for (size_t i = dotIndex + 1; i < path.length(); ++i) {
      if (path[i] == fs::path::preferred_separator) return true;
    }
    return false;
  }
  return false;
}

// Expand a glob pattern to matching files in a single directory.
std::vector<std::string> expand_glob_pattern(const std::string &pattern) {

  std::vector<std::string> matches;

  // If not a glob pattern and exists, add directly.
  if (!is_glob_pattern(pattern)) {
    if (fs::exists(pattern)) {
      matches.push_back(fs::path(pattern).string());
    } else {
      fprintf(stderr, "Path %s does not exist.\n", pattern.c_str());
    }
    return matches;
  }

  // Extract directory and filename pattern.
  fs::path glob_path(pattern);
  fs::path dir = glob_path.has_parent_path() ? glob_path.parent_path() : ".";
  std::string filename_pattern = glob_path.filename().string();
  std::string regex_pattern = glob_to_regex(filename_pattern);
  std::regex regex_pattern_compiled(regex_pattern, std::regex_constants::icase);
  // Check if directory exists.
  if (!fs::is_directory(dir)) {
    fprintf(stderr, "Directory %s does not exist.\n", dir.string().c_str());
    return matches;
  }

  // Collect matching files in the directory.
  try {
    for (const auto &entry : fs::directory_iterator(dir)) {
      if (entry.is_regular_file() &&
          std::regex_match(entry.path().filename().string(),
                            regex_pattern_compiled)) {
        matches.push_back(entry.path().string());
      }
    }
  } catch (const fs::filesystem_error &e) {
    fprintf(stderr, "Error accessing directory %s: %s\n",
                    dir.string().c_str(), e.what());
  }

  if (matches.empty()) {
    fprintf(stderr, "No files matched pattern: %s\n", pattern.c_str());
  }

  return matches;
}

// Recursively collect all files in a directory.
std::vector<std::string> walk_directory(const std::string &dir) {

  std::vector<std::string> files;

  try {
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file()) {
        files.push_back(entry.path().string());
      }
    }
  } catch (const fs::filesystem_error &e) {
    fprintf(stderr, "Error accessing directory %s: %s\n",
                    dir.c_str(), e.what());
  }

  if (files.empty()) {
    fprintf(stderr, "No files found in directory %s\n", dir.c_str());
  }

  return files;
}

// Group files by their parent directory for injection.
std::map<std::string, std::vector<std::string>>
group_files_by_subdir(const std::string &root_dir) {

  std::map<std::string, std::vector<std::string>> subdir_files;

  for (const auto &entry : fs::recursive_directory_iterator(root_dir)) {
    if (entry.is_regular_file()) {
      fs::path path = entry.path();
      std::string subdir = path.parent_path().filename().string();
      subdir_files[subdir].push_back(path.string());
    }
  }
  return subdir_files;
}

// Get output filename with appropriate extension based on entry name.
std::string get_output_filename(const std::string &name,
                                const std::string &output_dir) {

  size_t ext_pos = name.rfind('.');

  if (ext_pos == std::string::npos) {
    fprintf(stderr, "No extension found in filename: %s\n", name.c_str());
    return fs::path(output_dir).append(name).string();
  }

  std::string extension = toLower(name.substr(ext_pos + 1));
  std::string base_name = name.substr(0, ext_pos);
  std::string new_extension = extension;

  if (!as_is) {
    if (extension == "cbg" || extension == "mzp") {
      new_extension = "png";
    } else if (extension == "ctd") {
      new_extension = "txt";
    } else if (extension == "mp4" || extension == "chs" ||
                extension == "ccit" || extension == "hw") {
      new_extension = extension;
    }
  }

  fs::path output_path = output_dir;
  output_path.append(base_name + "." + new_extension);
  return output_path.string();
}

std::string get_extension(const std::string &name) {

  size_t pos = name.rfind('.');
  return pos != std::string::npos ? toLower(name.substr(pos)) : "";
}

std::string get_base_name(const std::string &name) {

  size_t pos = name.rfind('.');
  return pos != std::string::npos ? name.substr(0, pos) : name;
}

bool paths_from_file(const char *paths_file, std::vector<std::string> &paths) {
  std::string content;
  if (!mg::fs::read_file(paths_file, content)) {
    return false;
  }
  size_t start = 0;
  for (size_t i = 0; i <= content.size(); ++i) {
    if (i == content.size() || content[i] == '\n') {
      if (i > start) {
        paths.emplace_back(content.substr(start, i - start));
      }
      start = i + 1;
    }
  }
  return true;
}

// Helper function to open HFA archive.
std::unique_ptr<mg::data::HfaArchive>
open_hfa_archive(const std::string &path) {

  auto unique_file = mg::fs::MappedFile::open(path.c_str());

  if (!unique_file) {
    return nullptr;
  }

  auto file = std::shared_ptr<mg::fs::MappedFile>(std::move(unique_file));
  auto hfa = std::make_unique<mg::data::HfaArchive>(path);
  hfa->open(file);
  return hfa;
}

int listing_hfa(std::vector<std::string> &archives,
                  std::vector<std::string> &paths) {
  // Handle listing files in HFA archives.
  if (archives.empty()) {
    fprintf(stderr, "HFA archives or directory with them not "
                    "specified for listing files in them.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else if (archives.size() == 1 && fs::is_directory(archives[0])) {
    // Apply *.hfa pattern for directory.
    std::string pattern = archives[0] + "/*.hfa";
    archive_files = expand_glob_pattern(pattern);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No HFA archives found for listing files in them.\n");
    return -1;
  }

  if (!paths.empty() && (paths.size() > 1 || !isPotentialDirectory(paths[0]))) {
    fprintf(stderr, "Specify in --paths a directory for "
                    "listing files in HFA archives.\n");
    return -1;
  }

  fs::path output_dir, output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    auto hfa = open_hfa_archive(archive);

    if (!hfa) {
      fprintf(stderr, "Failed to open HFA archive: %s\n", archive.c_str());
      continue;
    }

    if (paths.empty()) {
      output_dir = fs::path(archive).parent_path() / "lists";
    } else {
      output_dir = paths[0];
    }

    output_path = output_dir / (fs::path(archive).stem().string() + ".txt");

    std::string output;

    for (const auto &entry : *hfa) {
      output += entry.filename() + "\n";
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory for %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }

    if (!mg::fs::write_file(output_path.string().c_str(), output)) {
      continue;
    }

    fprintf(stderr, "Wrote list to %s\n", output_path.string().c_str());
  }

  return 0;
}

int hfa_extract(std::vector<std::string> &archives,
                std::vector<std::string> &names,
                std::vector<std::string> &paths) {
  // Handle extracting files from HFA archives.
  if (archives.empty()) {
    fprintf(stderr, "HFA archives or directory with them "
                    "not specified for extract.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else if (archives.size() == 1 && fs::is_directory(archives[0])) {
    // Apply *.hfa pattern for directory.
    std::string pattern = archives[0] + "/*.hfa";
    archive_files = expand_glob_pattern(pattern);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No HFA archives found to extract.\n");
    return -1;
  }

  if (!names.empty() && archive_files.size() > 1) {
    fprintf(stderr, "Specify only one HFA archive when "
                    "specifying names of files to extract.\n");
    return -1;
  }

  if (!indexes.empty() && archive_files.size() > 1) {
    fprintf(stderr, "Specify only one HFA archive when specifying "
                    "the indexes of files to extract.\n");
    return -1;
  }

  auto extract_entry = [](auto &entry, const auto &path, int flags) -> bool {

    std::string entry_data;
    if (!entry.to_data(entry_data)) {
      fprintf(stderr, "Failed to read entry data.\n");
      return false;
    }

    std::string output_data;

    if (flags == 0) {
      output_data = entry_data;
    } else if (flags == 1) {
      mg::data::CompressedBG cbg(entry_data);
      if (!cbg.is_valid()) {
        fprintf(stderr, "Invalid CBG data in entry.\n");
        return false;
      }
      if (!cbg.img_write(output_data)) return false;
    } else if (flags == 2) {
      mg::data::MzpImage mzp(entry_data);
      if (!mzp.is_valid()) {
        fprintf(stderr, "Invalid MZP data in entry.\n");
        return false;
      }
      if (!mzp.img_write(output_data)) return false;
    } else if (flags == 3) {
      if (entry_data.compare(0, sizeof(mg::data::Lenzu::Lenzu_magic) - 1,
                              mg::data::Lenzu::Lenzu_magic) == 0) {
        if (!mg::data::Lenzu::decompress(entry_data, output_data, true)) {
          return false;
        }
      } else output_data = entry_data;
    }

    if (!mg::fs::write_file(path.c_str(), output_data)) {
        return false;
    }

    return true;
  };

  fs::path output_dir, out_dir;

  if (paths.empty() || (paths.size() == 1 && isPotentialDirectory(paths[0]))) {

    for (const auto &archive : archive_files) {

      if (!fs::is_regular_file(archive)) {
        fprintf(stderr, "%s does not exist.\n", archive.c_str());
        continue;
      }

      auto hfa = open_hfa_archive(archive);
      if (!hfa) {
        fprintf(stderr, "Failed to open HFA archive: %s\n", archive.c_str());
        continue;
      }

      if (!paths.empty()) {
        output_dir = paths[0];
      } else {
        output_dir = fs::path(archive).parent_path();
      }

      out_dir = output_dir / fs::path(archive).stem();

      if (!out_dir.empty() && !fs::is_directory(out_dir)) {
        if (!fs::create_directories(out_dir)) {
          fprintf(stderr, "Failed to create directory for %s\n",
                          out_dir.string().c_str());
          continue;
        }
      }

      if (!names.empty()) {
        // Expand glob patterns in names.
        std::vector<std::string> expanded_names;
        for (const auto &name : names) {
          if (is_glob_pattern(name)) {
            bool found = false;
            std::string regex_pattern = "^" + glob_to_regex(name) + "$";
            std::regex name_regex(regex_pattern, std::regex_constants::icase);
            for (const auto &entry : *hfa) {
              if (std::regex_match(entry.key(), name_regex)) {
                expanded_names.push_back(entry.key());
                found = true;
              }
            }

            if (!found) {
              fprintf(stderr, "No files matching '%s' found in '%s\n",
                              name.c_str(), archive.c_str());
              continue;
            }
          } else {
            if (hfa->contains(name)) {
              expanded_names.push_back(name);
            } else {
              fprintf(stderr, "File %s not found in %s\n",
                              name.c_str(), archive.c_str());
              continue;
            }
          }
        }

        // Extract matching names.
        for (const auto &name : expanded_names) {

          mg::data::HfaEntry &entry = (*hfa)[name];

          std::string output_path = get_output_filename(name, out_dir.string());

          int flags = as_is ? 0 : (ends_with(name, ".cbg") ? 1 :
                                  ends_with(name, ".mzp") ? 2 :
                                  ends_with(name, ".ctd") ? 3 : 0);

          if (!extract_entry(entry, output_path, flags)) {
            fprintf(stderr, "Failed to extract file No. %zu: %s from %s\n",
                            entry.index(), name.c_str(), archive.c_str());
            continue;
          }

          fprintf(stderr, "File No. %zu: %s extracted to %s\n", entry.index(),
                          name.c_str(), output_path.c_str());
        }
      } else if (!indexes.empty()) {

        for (auto index : indexes) {

          if (!hfa->contains(index)) {
            fprintf(stderr, "File %ld not contains in %s\n",
                            index, archive.c_str());
            continue;
          }

          mg::data::HfaEntry &entry = (*hfa)[index];

          std::string output_path =
              get_output_filename(entry.key(), out_dir.string());

          int flags = as_is ? 0 : (ends_with(entry.key(), ".cbg") ? 1 :
                                  ends_with(entry.key(), ".mzp") ? 2 :
                                  ends_with(entry.key(), ".ctd") ? 3 : 0);

          if (!extract_entry(entry, output_path, flags)) {
            fprintf(stderr, "Failed to extract file No. %lu: %s from %s\n",
                            index, entry.key().c_str(), archive.c_str());
            continue;
          }

          fprintf(stderr, "File No. %lu: %s extracted to %s\n", index,
                          entry.key().c_str(), output_path.c_str());
        }
      } else {
        // Extract all files.
        for (const auto &entry : *hfa) {

          std::string output_path =
              get_output_filename(entry.key(), out_dir.string());

          if (fs::is_regular_file(output_path)) {
            fprintf(stderr, "File No. %zu: %s already exists, skipping.\n",
                            entry.index(), output_path.c_str());
            continue;
          }

          int flags = as_is ? 0 : (ends_with(entry.key(), ".cbg") ? 1 :
                                  ends_with(entry.key(), ".mzp") ? 2 :
                                  ends_with(entry.key(), ".ctd") ? 3 : 0);

          if (!extract_entry(entry, output_path, flags)) {
            fprintf(stderr, "Failed to extract file No. %zu: %s from %s\n",
                            entry.index(), entry.key().c_str(),
                            archive.c_str());
            continue;
          }

          fprintf(stderr, "File No. %zu: %s extracted to %s\n", entry.index(),
                          entry.key().c_str(), output_path.c_str());
        }
      }
    }
  } else if (!paths.empty()) {
    // Single archive with list of files.
    if (archive_files.size() != 1) {
      fprintf(stderr, "Specify only one HFA archive when "
                      "specifying files to extract.\n");
      return -1;
    }

    std::string archive = archive_files[0];

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      return -1;
    }

    auto hfa = open_hfa_archive(archive);
    if (!hfa) {
      fprintf(stderr, "Failed to open HFA archive: %s\n", archive.c_str());
      return -1;
    }

    for (const auto &file : paths) {
      output_dir = fs::path(file).parent_path();
      std::string filename = fs::path(file).filename().string();
      std::string base_name = fs::path(file).stem().string();
      std::string fileExt = toLower(fs::path(file).extension().string());
      std::string candidate_name;

      bool found = false;

      for (auto &ext : KNOWN_EXTENSIONS) {

        candidate_name = base_name + ext;

        if (hfa->contains(candidate_name)) {
          found = true;
          mg::data::HfaEntry &entry = (*hfa)[candidate_name];
          std::string itExt = toLower(get_extension(entry.key()));

          int flags = (fileExt != ".cbg" && itExt == ".cbg") ? 1 :
                      (fileExt != ".mzp" && itExt == ".mzp") ? 2 :
                      (fileExt != ".ctd" && itExt == ".ctd") ? 3 : 0;

          if (!output_dir.empty() && !fs::is_directory(output_dir)) {
            if (!fs::create_directories(output_dir)) {
              fprintf(stderr, "Failed to create directory for %s\n",
                              output_dir.string().c_str());
              break;
            }
          }

          std::string output_data;
          if (!extract_entry(entry, file, flags)) {
            fprintf(stderr, "Failed to extract file No. %zu: %s from %s\n",
                            entry.index(), entry.key().c_str(),
                            archive.c_str());
            break;
          }

          fprintf(stderr, "File No. %zu: %s extracted to %s\n", entry.index(),
                          entry.key().c_str(), file.c_str());
          break;
        }
      }

      if (!found) {
        fprintf(stderr, "File %s not found in %s\n",
                        filename.c_str(), archive.c_str());
        continue;
      }
    }
  }

  return 0;
}

int hfa_inject(std::vector<std::string> &archives,
                std::vector<std::string> &paths,
                std::string &template_arc) {
  // Handle injecting files into HFA archives.
  if (archives.size() != 1) {
    fprintf(stderr, "Specify only one HFA archive or directory "
                    "for archives when inserting files.\n");
    return -1;
  }

  if (paths.empty()) {
    fprintf(stderr, "Files or directory with them not specified "
                    "for injecting into HFA archives.\n");
    return -1;
  }

  if (template_arc.empty()) {
    template_arc = archives[0];
  } else {
    if (fs::is_directory(template_arc) && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify a directory in --archives when "
                      "specifying the directory in --template.\n");
      return -1;
    }
  }

  if (!fs::exists(template_arc)) {
    fprintf(stderr, "Template path %s does not exist.\n", template_arc.c_str());
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && !fs::is_directory(template_arc) &&
               fs::is_directory(paths[0])) {
      input_paths = walk_directory(paths[0]);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No files found for injecting into HFA archives.\n");
    return -1;
  }

  auto inject_entry = [](auto &entry, const auto &file) -> bool {

    std::string file_data;
    if (!mg::fs::read_file(file.c_str(), file_data)) {
      return false;
    }

    size_t ext_pos = entry.filename().rfind('.');
    if (ext_pos == std::string::npos) {
      fprintf(stderr, "No extension found in filename: %s\n",
                      entry.key().c_str());
      return false;
    }

    std::string extension = toLower(entry.filename().substr(ext_pos + 1));
    std::string filename = toLower(fs::path(file).filename().string());

    if (filename.size() >= extension.size() &&
        filename.compare(filename.size() - extension.size(),
                          extension.size(), extension) == 0) {
      return entry.from_data(file_data);
    }

    std::string entry_data;
    if (!entry.to_data(entry_data)) {
      fprintf(stderr, "Failed to read entry data.\n");
      return false;
    }

    std::string input_data;

    if (extension == "cbg") {
      mg::data::CompressedBG cbg(entry_data);
      if (!cbg.is_valid()) {
        fprintf(stderr, "Invalid CBG data in entry.\n");
        return false;
      }
      if (!cbg.img_read(file_data)) {
        fprintf(stderr, "Failed to convert image to CBG.\n");
        return false;
      }
      if (!cbg.cbg_write(input_data)) {
        fprintf(stderr, "Failed to write CBG data.\n");
        return false;
      }
      return entry.from_data(input_data);
    } else if (extension == "mzp") {
      mg::data::MzpImage mzp(entry_data);
      if (!mzp.is_valid()) {
        fprintf(stderr, "Invalid MZP data in entry.\n");
        return false;
      }
      if (!mzp.img_read(file_data, 2)) {
        fprintf(stderr, "Failed to convert image to MZP.\n");
        return false;
      }
      if (!mzp.write_mzp(input_data)) {
        fprintf(stderr, "Failed to write MZP data.\n");
        return false;
      }
      return entry.from_data(input_data);
    } else if (extension == "ctd" || extension == "ccit" ||
                extension == "chs" || extension == "mp4" ||
                extension == "hw") {
      return entry.from_data(file_data);
    }

    fprintf(stderr, "Unsupported extension for inject: %s\n",
                    extension.c_str());
    return false;
  };

  fs::path archive_path, archive_dir;

  // Check if archives and paths are directories.
  if (!paths.empty() && fs::is_directory(template_arc) &&
      fs::is_directory(paths[0])) {
    // Group files by parent directory.
    auto subdir_files = group_files_by_subdir(paths[0]);

    for (const auto &[subdir, files] : subdir_files) {
      // Construct archive path: archives/subdir.hfa.
      fs::path arc = fs::path(template_arc) / (subdir + ".hfa");

      if (!fs::is_regular_file(arc)) {
        fprintf(stderr, "Archive %s does not exist.\n", arc.string().c_str());
        continue;
      }

      if (isPotentialDirectory(archives[0])) {
        archive_path = fs::path(archives[0]) / (subdir + ".hfa");
        archive_dir = archives[0];
      }

      // Open archive.
      auto hfa = open_hfa_archive(arc.string());
      if (!hfa) {
        fprintf(stderr, "Failed to open HFA archive: %s\n",
                        arc.string().c_str());
        continue;
      }

      bool success = true;

      std::unordered_map<std::string, std::string> names_map;

      for (const auto &entry : *hfa) {
        const std::string &full_name = entry.key();
        std::string base_name = get_base_name(full_name);
        names_map[base_name] = full_name;
      }

      for (const auto &file : files) {

        if (!fs::is_regular_file(file)) {
          fprintf(stderr, "%s does not exist.\n", file.c_str());
          continue;
        }

        std::string filename = fs::path(file).filename().string();
        std::string base_name = fs::path(file).stem().string();
        auto it = names_map.find(base_name);
        if (it == names_map.end()) {
          fprintf(stderr, "File %s not found in %s\n", filename.c_str(),
                          arc.string().c_str());
          continue;
        }

        const std::string &archive_filename = it->second;
        mg::data::HfaEntry &entry = (*hfa)[archive_filename];

        if (!inject_entry(entry, file)) {
          fprintf(stderr, "Failed to inject %s to pos. %zu into %s\n",
                          file.c_str(), entry.index(),
                          archive_path.string().c_str());
          success = false;
          break;
        }

        fprintf(stderr, "Injected %s to pos. %zu into %s\n", file.c_str(),
                        entry.index(), archive_path.string().c_str());
      }
      // Write archive if all injections succeeded.
      if (success) {
        fprintf(stderr, "Write archive %s\n", archive_path.string().c_str());
        if (!archive_dir.empty() && !fs::is_directory(archive_dir)) {
          if (!fs::create_directories(archive_dir)) {
            fprintf(stderr, "Failed to create directory %s\n",
                            archive_dir.string().c_str());
            continue;
          }
        }

        std::string output_data;
        if (!hfa->hfa_write(output_data)) {
          fprintf(stderr, "Failed to generate archive data for %s\n",
                          archive_path.string().c_str());
          continue;
        }

        if (!mg::fs::write_file(archive_path.string().c_str(), output_data)) {
          continue;
        }

        fprintf(stderr, "Wrote archive %s\n", archive_path.string().c_str());
      } else {
        fprintf(stderr, "Injection failed for %s, skipping write.\n",
                        archive_path.string().c_str());
        continue;
      }
    }
  } else if (!paths.empty()) {
    // Single archive mode with path to directory or individual files.
    if (!fs::is_regular_file(template_arc)) {
      fprintf(stderr, "Template archive %s does not exists.\n",
                      template_arc.c_str());
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archive_path = fs::path(archives[0]) / fs::path(template_arc).filename();
    } else {
      archive_path = archives[0];
    }

    archive_dir = archive_path.parent_path();

    auto hfa = open_hfa_archive(template_arc);
    if (!hfa) {
      fprintf(stderr, "Failed to open HFA archive: %s\n", template_arc.c_str());
      return -1;
    }

    bool success = true;

    std::unordered_map<std::string, std::string> names_map;

    for (const auto &entry : *hfa) {
      const std::string &full_name = entry.key();
      std::string base_name = get_base_name(full_name);
      names_map[base_name] = full_name;
    }

    for (const auto &file : input_paths) {

      if (!fs::is_regular_file(file)) {
        fprintf(stderr, "%s does not exist.\n", file.c_str());
        continue;
      }

      std::string filename = fs::path(file).filename().string();
      std::string base_name = fs::path(file).stem().string();
      auto it = names_map.find(base_name);
      if (it == names_map.end()) {
        fprintf(stderr, "File %s not found in %s\n",
                        filename.c_str(), template_arc.c_str());
        continue;
      }

      const std::string &archive_filename = it->second;
      mg::data::HfaEntry &entry = (*hfa)[archive_filename];

      if (!inject_entry(entry, file)) {
        fprintf(stderr, "Failed to inject %s to pos. %zu into %s\n",
                        file.c_str(), entry.index(),
                        archive_path.string().c_str());
        success = false;
        break;
      }

      fprintf(stderr, "Injected %s to pos. %zu into %s\n", file.c_str(),
                      entry.index(), archive_path.string().c_str());
    }

    if (success) {
      fprintf(stderr, "Write archive %s\n", archive_path.string().c_str());
      if (!archive_dir.empty() && !fs::is_directory(archive_dir)) {
        if (!fs::create_directories(archive_dir)) {
          fprintf(stderr, "Failed to create directory for %s\n",
                          archive_path.string().c_str());
          return -1;
        }
      }
      std::string output_data;
      if (!hfa->hfa_write(output_data)) {
        fprintf(stderr, "Failed to generate archive data for %s\n",
                        archive_path.string().c_str());
        return -1;
      }
      if (!mg::fs::write_file(archive_path.string().c_str(), output_data)) {
        return -1;
      }

      fprintf(stderr, "Wrote archive %s\n", archive_path.string().c_str());

    } else {
      fprintf(stderr, "Injection failed for %s, skipping write.\n",
                      archive_path.string().c_str());
      return -1;
    }
  }

  return 0;
}

int hfa_add_files(std::vector<std::string> &archives,
                std::vector<std::string> &paths,
                std::string &template_arc) {
  // Handle adding files into HFA archives.
  if (archives.size() != 1) {
    fprintf(stderr, "Specify only one HFA archive or output directory "
                    "for archive when adding files.\n");
    return -1;
  }

  if (paths.empty()) {
    fprintf(stderr, "Files or directory with them not specified "
                    "for adding files into HFA archives.\n");
    return -1;
  }

  if (template_arc.empty()) {
    template_arc = archives[0];
  }

  if (isPotentialDirectory(template_arc)) {
    fprintf(stderr, "Specify the existing archive in --template "
                    "or path to new archive in --archives.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && !fs::is_directory(template_arc) &&
               fs::is_directory(paths[0])) {
      input_paths = walk_directory(paths[0]);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No files found for adding into HFA archive.\n");
    return -1;
  }

  // Check if indexes size matches input_paths size when indexes is not empty.
  if (!indexes.empty() && indexes.size() != input_paths.size()) {
    fprintf(stderr, "Mismatch between paths (%zu) and indexes (%zu) sizes\n",
                    input_paths.size(), indexes.size());
    return -1;
  }

  fs::path archive_path, archive_dir;

  if (isPotentialDirectory(archives[0])) {
    archive_path = fs::path(archives[0]) / fs::path(template_arc).filename();
  } else {
    archive_path = archives[0];
  }

  archive_dir = archive_path.parent_path();

  std::unique_ptr<mg::data::HfaArchive> hfa;

  // Create or open archive.
  if (!fs::is_regular_file(template_arc)) {
    fprintf(stderr, "Create new archive %s\n", template_arc.c_str());
    hfa = std::make_unique<mg::data::HfaArchive>(template_arc);
    if (!hfa) {
      fprintf(stderr, "Failed to create HFA archive: %s\n",
                      template_arc.c_str());
      return -1;
    }
  } else {
    hfa = open_hfa_archive(template_arc);
    if (!hfa) {
      fprintf(stderr, "Failed to open HFA archive: %s\n",
                      template_arc.c_str());
      return -1;
    }
  }

  auto index_it = indexes.begin();

  size_t added_count = 0;

  for (const auto &file : input_paths) {

    if (!fs::is_regular_file(file)) {
      fprintf(stderr, "%s does not exist.\n", file.c_str());
      if (!indexes.empty()) ++index_it;
      continue;
    }

    std::string filename = fs::path(file).filename().string();
    if (hfa->contains(filename)) {
      fprintf(stderr, "File %s already exist in %s\n",
                      filename.c_str(), template_arc.c_str());
      if (!indexes.empty()) ++index_it;
      continue;
    }

    std::string file_data;
    if (!mg::fs::read_file(file.c_str(), file_data)) {
      if (!indexes.empty()) ++index_it;
      continue;
    }

    // Use index from set if available, otherwise use -1.
    ssize_t index = indexes.empty() ? -1 : *index_it;
    ssize_t added_index = hfa->add_entry(filename, file_data, index);
    if (added_index == -1) {
      fprintf(stderr, "Failed update %s error adding %s to pos. %zd\n",
                      archive_path.string().c_str(), file.c_str(), added_index);
      return -1;
    }

    fprintf(stderr, "Added %s to pos. %zd into %s\n", file.c_str(),
                    added_index, archive_path.string().c_str());

    ++added_count;
    if (!indexes.empty()) ++index_it;
  }

  if (!added_count) {
    fprintf(stderr, "No files added, archive update canceled.\n");
    return -1;
  } else {
    fprintf(stderr, "Added %zu entries from %zu files.\n",
                    added_count, input_paths.size());
  }

  fprintf(stderr, "Write archive %s\n", archive_path.string().c_str());

  if (!archive_dir.empty() && !fs::is_directory(archive_dir)) {
    if (!fs::create_directories(archive_dir)) {
      fprintf(stderr, "Failed to create directory for %s\n",
                      archive_path.string().c_str());
      return -1;
    }
  }

  std::string output_data;
  if (!hfa->hfa_write(output_data)) {
    fprintf(stderr, "Failed to generate archive data for %s\n",
                    archive_path.string().c_str());
    return -1;
  }

  if (!mg::fs::write_file(archive_path.string().c_str(), output_data)) {
    return -1;
  }

  fprintf(stderr, "Wrote archive %s\n", archive_path.string().c_str());

  return 0;
}

int lenzu_decompress(std::vector<std::string> &archives,
                        std::vector<std::string> &paths) {
  // Handle decompress lenzu files.

  if (archives.empty()) {
    fprintf(stderr, "Lenzu files are not specified for decompressing.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;

  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No lenzu files found for decompression.\n");
    return -1;
  }

  fs::path output_dir;

  if (!paths.empty()) {
    if (paths.size() > 1) {
      fprintf(stderr, "Specify only one one output file "
                      "or directory for them.\n");
      return -1;
    }
    if (!isPotentialDirectory(paths[0]) && archive_files.size() > 1){
      fprintf(stderr, "Specify only one lenzu file when "
                      "specifying output file.\n");
      return -1;
    }

    if (isPotentialDirectory(paths[0])) {
      output_dir = paths[0];
    } else {
      output_dir = fs::path(paths[0]).parent_path();
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    if (!paths.empty()) {
      if (isPotentialDirectory(paths[0])) {
        output_path = output_dir / (fs::path(archive).stem().string() + ".txt");
      } else {
        output_path = paths[0];
      }
    } else {
      output_path = fs::path(archive).parent_path() /
                    (fs::path(archive).stem().string() + ".txt");
    }

    std::string src;
    if (!mg::fs::read_file(archive.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::Lenzu::decompress(src, dest, true)) {
      fprintf(stderr, "Failed to decompress %s\n", archive.c_str());
      continue;
    }

    if (!mg::fs::write_file(output_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Decompressed %s to %s\n", archive.c_str(),
                    output_path.string().c_str());
  }

  return 0;
}

int ccit_to_txt(std::vector<std::string> &archives,
                    std::vector<std::string> &paths) {
  // Handle conversion CCIT to TXT table.

  if (archives.empty()) {
    fprintf(stderr, "CCIT files not specified for "
                    "converting to a text table.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No CCIT files found for converting to a text table.\n");
    return -1;
  }

  fs::path output_dir;

  if (!paths.empty()) {
    if (paths.size() > 1) {
      fprintf(stderr, "Specify only one one output file "
                      "or directory for them.\n");
      return -1;
    }
    if (!isPotentialDirectory(paths[0]) && archive_files.size() > 1) {
      fprintf(stderr, "Specify only one CCIT file when "
                      "specifying output file.\n");
      return -1;
    }

    if (isPotentialDirectory(paths[0])) {
      output_dir = paths[0];
    } else {
      output_dir = fs::path(paths[0]).parent_path();
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    if (!paths.empty()) {
      if (isPotentialDirectory(paths[0])) {
        output_path = output_dir / (fs::path(archive).stem().string() + ".txt");
      } else {
        output_path = paths[0];
      }
    } else {
      output_path = fs::path(archive).parent_path() /
                    (fs::path(archive).stem().string() + ".txt");
    }

    std::string src;
    if (!mg::fs::read_file(archive.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::ccit_to_txt(src, dest)) {
      fprintf(stderr, "Failed to convert %s to TXT.\n", archive.c_str());
      continue;
    }

    if (!mg::fs::write_file(output_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Converted %s to %s\n", archive.c_str(),
                    output_path.string().c_str());
  }

  return 0;
}

int txt_to_ccit(std::vector<std::string> &paths,
                std::vector<std::string> &archives) {
  // Handle conversion TXT table to CCIT.
  if (paths.empty()) {
    fprintf(stderr, "No TXT files specified for convertng to CCIT.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No files found for converting to CCIT.\n");
    return -1;
  }

  fs::path archives_dir;

  if (!archives.empty()) {
    if (archives.size() > 1) {
      fprintf(stderr, "Specify in --archives only one "
                      "CCIT file or directory for them.\n");
      return -1;
    }
    if (input_paths.size() > 1 && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify only one input file when "
                      "specifying a CCIT file.\n");
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archives_dir = archives[0];
    } else {
      archives_dir = fs::path(archives[0]).parent_path();
    }

    if (!archives_dir.empty() && !fs::is_directory(archives_dir)) {
      if (!fs::create_directories(archives_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        archives_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path archive_path;

  for (const auto &path : input_paths) {

    if (!fs::is_regular_file(path)) {
      fprintf(stderr, "%s does not exist.\n", path.c_str());
      continue;
    }

    if (archives.empty()) {
      archive_path = fs::path(path).parent_path() /
                     (fs::path(path).stem().string() + ".ccit");
    } else if (isPotentialDirectory(archives[0])) {
      archive_path = archives_dir / (fs::path(path).stem().string() + ".ccit");
    } else {
      archive_path = archives[0];
    }

    std::string src;
    if (!mg::fs::read_file(path.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::txt_to_ccit(src, dest)) {
      fprintf(stderr, "Failed to convert %s to CCIT.\n", path.c_str());
      continue;
    }

    if (!mg::fs::write_file(archive_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Converted %s to %s\n", path.c_str(),
                    archive_path.string().c_str());
  }

  return 0;
}

int cbg_to_png(std::vector<std::string> &archives,
                  std::vector<std::string> &paths) {
  // Handle conversion CGB to PNG.
  if (archives.empty()) {
    fprintf(stderr, "CBG files not specified for converting to PNG.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else if (archives.size() == 1 && fs::is_directory(archives[0])) {
    // Apply *.cgb pattern for directory.
    std::string pattern = archives[0] + "/*.cbg";
    archive_files = expand_glob_pattern(pattern);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No CBG files found for converting to PNG.\n");
    return -1;
  }

  fs::path output_dir;

  if (!paths.empty()) {
    if (paths.size() > 1) {
      fprintf(stderr, "Specify only one one output file "
                      "or directory for them.\n");
      return -1;
    }
    if (!isPotentialDirectory(paths[0]) && archive_files.size() > 1) {
      fprintf(stderr, "Specify only one .cbg file when "
                      "specifying output file.\n");
      return -1;
    }
    if (fs::is_directory(archives[0]) && !isPotentialDirectory(paths[0])) {
      fprintf(stderr, "Specify a directory for output .png files when "
                      "specifying the directory with .cbg files.\n");
      return -1;
    }

    if (isPotentialDirectory(paths[0])) {
      output_dir = paths[0];
    } else {
      output_dir = fs::path(paths[0]).parent_path();
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    if (!paths.empty()) {
      if (isPotentialDirectory(paths[0])) {
        output_path = output_dir / (fs::path(archive).stem().string() + ".png");
      } else {
        output_path = paths[0];
      }
    } else {
      output_path = fs::path(archive).parent_path() /
                    (fs::path(archive).stem().string() + ".png");
    }

    std::string cbg_data;
    if (!mg::fs::read_file(archive.c_str(), cbg_data)) {
      continue;
    }

    mg::data::CompressedBG cbg(cbg_data);
    if (!cbg.is_valid()) {
      fprintf(stderr, "Invalid CBG data in %s\n", archive.c_str());
      continue;
    }

    std::string output;
    if (!cbg.img_write(output)) {
      fprintf(stderr, "Failed to convert %s to PNG.\n", archive.c_str());
      continue;
    }

    if (!mg::fs::write_file(output_path.string().c_str(), output)) {
      continue;
    }

    fprintf(stderr, "Converted CBG %s to %s\n", archive.c_str(),
                    output_path.string().c_str());
  }

  return 0;
}

int png_to_cbg(std::vector<std::string> &paths,
                std::vector<std::string> &archives,
                          std::string &template_arc) {
  // Handle conversion PNG to CBG.
  if (paths.empty()) {
    fprintf(stderr, "PNG files not specified for converting to CBG.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && fs::is_directory(paths[0])) {
      // Apply *.png pattern for directory.
      std::string pattern = paths[0] + "/*.png";
      input_paths = expand_glob_pattern(pattern);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No PNG files found for converting to CBG.\n");
    return -1;
  }

  if (!template_arc.empty()) {
    if (fs::is_directory(template_arc) && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify a directory in --archives when "
                      "specifying the directory in --template.\n");
      return -1;
    }
  }

  fs::path archives_dir;

  if (!archives.empty()) {
    if (archives.size() > 1) {
      fprintf(stderr, "Specify in --archives only one "
                      ".cbg file or directory for them.\n");
      return -1;
    }
    if (input_paths.size() > 1 && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify only one .png when "
                      "specifying the .cbg file.\n");
      return -1;
    }
    if (fs::is_directory(paths[0]) && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify a directory for .cbg files when "
                      "specifying the directory with .png files.\n");
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archives_dir = archives[0];
    } else {
      archives_dir = fs::path(archives[0]).parent_path();
    }

    if (!archives_dir.empty() && !fs::is_directory(archives_dir)) {
      if (!fs::create_directories(archives_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        archives_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path archive_path, archive;

  for (const auto &path : input_paths) {

    if (!fs::is_regular_file(path)) {
      fprintf(stderr, "%s does not exist.\n", path.c_str());
      continue;
    }

    if (template_arc.empty()) {
      archive = fs::path(path).parent_path() /
                (fs::path(path).stem().string() + ".cbg");
    } else if (fs::is_directory(template_arc)) {
      archive = fs::path(template_arc) /
                (fs::path(path).stem().string() + ".cbg");
    } else {
      archive = template_arc;
    }

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.string().c_str());
      continue;
    }

    if (archives.empty()) {
      archive_path = archive;
    } else if (isPotentialDirectory(archives[0])) {
      archive_path = archives_dir / (fs::path(path).stem().string() + ".cbg");
    } else {
      archive_path = archives[0];
    }

    std::string cbg_data;
    if (!mg::fs::read_file(archive.string().c_str(), cbg_data)) {
      continue;
    }

    mg::data::CompressedBG cbg(cbg_data);
    if (!cbg.is_valid()) {
      fprintf(stderr, "Invalid CBG data in %s\n", archive.string().c_str());
      continue;
    }

    std::string png_data;
    if (!mg::fs::read_file(path.c_str(), png_data)) {
      continue;
    }

    if (!cbg.img_read(png_data)) {
      fprintf(stderr, "Failed to convert %s to CBG.\n", path.c_str());
      continue;
    }

    std::string output;
    if (!cbg.cbg_write(output)) {
      fprintf(stderr, "Failed to write CBG data for %s\n",
                      archive_path.string().c_str());
      continue;
    }

    if (!mg::fs::write_file(archive_path.string().c_str(), output)) {
      continue;
    }

    fprintf(stderr, "Converted %s to %s\n", path.c_str(),
                    archive_path.string().c_str());
  }

  return 0;
}

int mzp_to_png(std::vector<std::string> &archives,
                  std::vector<std::string> &paths) {
  // Handle conversion MZP to PNG.

  if (archives.empty()) {
    fprintf(stderr, "MZP archives not specified for converting to PNG.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else if (archives.size() == 1 && fs::is_directory(archives[0])) {
    // Apply *.mzp pattern for directories.
    std::string pattern = archives[0] + "/*.mzp";
    archive_files = expand_glob_pattern(pattern);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No MZP files found for converting to PNG.\n");
    return -1;
  }

  fs::path output_dir;

  if (!paths.empty()) {
    if (paths.size() > 1) {
      fprintf(stderr, "Specify only one one output file "
                      "or directory for them.\n");
      return -1;
    }
    if (!isPotentialDirectory(paths[0]) && archive_files.size() > 1) {
      fprintf(stderr, "Specify only one .mzp file when "
                      "specifying output file.\n");
      return -1;
    }
    if (fs::is_directory(archives[0]) && !isPotentialDirectory(paths[0])) {
      fprintf(stderr, "Specify a directory for output .png files when "
                      "specifying the directory with .mzp files.\n");
      return -1;
    }

    if (isPotentialDirectory(paths[0])) {
      output_dir = paths[0];
    } else {
      output_dir = fs::path(paths[0]).parent_path();
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    if (!paths.empty()) {
      if (isPotentialDirectory(paths[0])) {
        output_path = output_dir / (fs::path(archive).stem().string() + ".png");
      } else {
        output_path = paths[0];
      }
    } else {
      output_path = fs::path(archive).parent_path() /
                    (fs::path(archive).stem().string() + ".png");
    }

    std::string mzp_data;
    if (!mg::fs::read_file(archive.c_str(), mzp_data)) {
      continue;
    }

    mg::data::MzpImage mzp(mzp_data);
    if (!mzp.is_valid()) {
      fprintf(stderr, "Invalid MZP data in %s\n", archive.c_str());
      continue;
    }

    std::string output;
    if (!mzp.img_write(output)) {
      fprintf(stderr, "Failed to convert %s to PNG.\n", archive.c_str());
      continue;
    }

    if (!mg::fs::write_file(output_path.string().c_str(), output)) {
      continue;
    }

    fprintf(stderr, "Converted MZP %s to %s\n", archive.c_str(),
                    output_path.string().c_str());
  }

  return 0;
}

int png_to_mzp(std::vector<std::string> &paths,
                std::vector<std::string> &archives,
                std::string &template_arc) {
  // Handle conversion PNG to MZP.
  if (paths.empty()) {
    fprintf(stderr, "PNG files not specified for converting to MZP.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && fs::is_directory(paths[0])) {
      // Apply *.png pattern for directory.
      std::string pattern = paths[0] + "/*.png";
      input_paths = expand_glob_pattern(pattern);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No PNG files found for converting to MZP.\n");
    return -1;
  }

  if (!template_arc.empty()) {
    if (fs::is_directory(template_arc) && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify a directory in --archives when "
                      "specifying the directory in --template.\n");
      return -1;
    }
  }

  fs::path archives_dir;

  if (!archives.empty()) {
    if (archives.size() > 1) {
      fprintf(stderr, "Specify in --archives only one "
                      ".mzp file or directory for them.\n");
      return -1;
    }
    if (input_paths.size() > 1 && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify only one .png file when "
                      "specifying an .mzp file.\n");
      return -1;
    }
    if (fs::is_directory(paths[0]) && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify a directory for .mzp files when "
                      "specifying the directory with .png files.\n");
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archives_dir = archives[0];
    } else {
      archives_dir = fs::path(archives[0]).parent_path();
    }

    if (!archives_dir.empty() && !fs::is_directory(archives_dir)) {
      if (!fs::create_directories(archives_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        archives_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path archive_path, archive;

  for (const auto &path : input_paths) {

    if (!fs::is_regular_file(path)) {
      fprintf(stderr, "%s does not exist.\n", path.c_str());
      continue;
    }

    if (template_arc.empty()) {
      archive = fs::path(path).parent_path() /
                (fs::path(path).stem().string() + ".mzp");
    } else if (fs::is_directory(template_arc)) {
      archive = fs::path(template_arc) /
                (fs::path(path).stem().string() + ".mzp");
    } else {
      archive = template_arc;
    }

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.string().c_str());
      continue;
    }

    if (archives.empty()) {
      archive_path = archive;
    } else if (isPotentialDirectory(archives[0])) {
      archive_path = archives_dir / (fs::path(path).stem().string() + ".mzp");
    } else {
      archive_path = archives[0];
    }

    std::string mzp_data;
    if (!mg::fs::read_file(archive.string().c_str(), mzp_data)) {
      continue;
    }

    mg::data::MzpImage mzp(mzp_data);
    if (!mzp.is_valid()) {
      fprintf(stderr, "Invalid MZP data in %s\n", archive.string().c_str());
      continue;
    }

    std::string png_data;
    if (!mg::fs::read_file(path.c_str(), png_data)) {
      continue;
    }

    if (!mzp.img_read(png_data, 2)) {
      fprintf(stderr, "Failed to convert %s to MZP.\n", path.c_str());
      continue;
    }

    std::string output;
    if (!mzp.write_mzp(output)) {
      fprintf(stderr, "Failed to write MZP data for %s\n",
                      archive_path.string().c_str());
      continue;
    }
    if (!mg::fs::write_file(archive_path.string().c_str(), output)) {
      continue;
    }

    fprintf(stderr, "Converted %s to %s\n", path.c_str(),
                    archive_path.string().c_str());
  }

  return 0;
}

int mzp_info(std::vector<std::string> &archives) {
  // Handle information for MZP archive.
  if (archives.size() != 1) {
    fprintf(stderr, "Specify only one MZP archive to "
                    "get information about it.\n");
    return -1;
  }

  if (!fs::is_regular_file(archives[0])) {
    fprintf(stderr, "%s does not exist.\n", archives[0].c_str());
    return -1;
  }

  std::string data;
  if (!mg::fs::read_file(archives[0].c_str(), data)) {
    return -1;
  }

  mg::data::Mzp mzp;
  if (!mg::data::mzp_read(data, mzp)) {
    fprintf(stderr, "Invalid MZP archive %s\n", archives[0].c_str());
    return -1;
  }

  fprintf(stderr, "MZP archive of %zu elements:\n", mzp.entry_headers.size());

  for (size_t i = 0; i < mzp.entry_headers.size(); ++i) {
    fprintf(stderr, "MzpArchiveEntry %zu:\n", i);
    mzp.entry_headers[i].print();
  }

  for (unsigned i = 0; i < mzp.entry_headers.size(); i++) {
    for (unsigned j = i + 1; j < mzp.entry_headers.size(); j++) {
      // Do these ranges overlap.
      auto &entry_i = mzp.entry_headers[i];
      auto &entry_j = mzp.entry_headers[j];
      const uint32_t entry_i_start = entry_i.data_offset_relative();
      const uint32_t entry_i_end = entry_i_start + entry_i.entry_data_size();
      const uint32_t entry_j_start = entry_j.data_offset_relative();
      if (entry_j_start >= entry_i_start && entry_j_start <= entry_i_end) {
        fprintf(stderr, "Entry %u begins inside of entry %u\n", j, i);
      }
    }
  }

  return 0;
}

int mzp_extract(std::vector<std::string> & archives,
                  std::vector<std::string> & paths) {
  // Handle extracting files from MZP archives.
  if (archives.empty()) {
    fprintf(stderr, "MZP archives not specified for extracting entries.\n");
    return -1;
  }

  if (!paths.empty() && !isPotentialDirectory(paths[0])) {
    fprintf(stderr, "Specify in --paths directory for extract MZP archives.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else if (archives.size() == 1 && fs::is_directory(archives[0])) {
    // Apply *.mzp pattern for directory.
    std::string pattern = archives[0] + "/*.mzp";
    archive_files = expand_glob_pattern(pattern);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No MZP archives found to extract entries.\n");
    return -1;
  }

  fs::path output_dir, output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    output_dir = (paths.empty() ? fs::path(archive).parent_path() :
                  fs::path(paths[0])) / fs::path(archive).stem();

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        continue;
      }
    }

    std::string data;
    if (!mg::fs::read_file(archive.c_str(), data)) {
      continue;
    }

    mg::data::Mzp mzp;
    if (!mg::data::mzp_read(data, mzp)) {
      fprintf(stderr, "Invalid MZP archive %s\n", archive.c_str());
      continue;
    }

    for (size_t i = 0; i < mzp.entry_headers.size(); ++i) {
      output_path = output_dir / mg::string::format("%s_%04zu.bin",
                    fs::path(archive).filename().string().c_str(), i);

      if (!mg::fs::write_file(output_path.string().c_str(),
                              mzp.entry_data[i])) {
        break;
      }

      fprintf(stderr, "Extracted %zu bytes to %s\n", mzp.entry_data[i].size(),
                      output_path.string().c_str());
    }
  }

  return 0;
}

int mzp_pack(std::vector<std::string> &paths,
                std::vector<std::string> &archives) {
  // Handle creating MZP archive.
  if (archives.size() != 1) {
    fprintf(stderr, "Specify only one MZP archive for injecting files.\n");
    return -1;
  }

  // Expand paths if necessary.
  fs::path input_dir;
  std::vector<std::string> input_files;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_files = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && fs::is_directory(paths[0])) {
      input_dir = paths[0];
    } else {
      input_files = paths;
    }
  } else {
    input_dir = fs::path(archives[0]).parent_path() /
                fs::path(archives[0]).stem();
  }

  if (!input_dir.empty()) {
    // Apply *.bin pattern for directory.
    std::string pattern = input_dir.string() + "/*.bin";
    input_files = expand_glob_pattern(pattern);
  }

  if (input_files.empty()) {
    fprintf(stderr, "No files found for injecting into MZP archive.\n");
    return -1;
  }

  fs::path output_dir = fs::path(archives[0]).parent_path();

  // Construct new MZP.
  mg::data::Mzp mzp;

  int count = 0;
  // Read each input file as a new MZP record.
  for (const auto &input_file : input_files) {

    if (!fs::is_regular_file(input_file)) {
      fprintf(stderr, "File: %s does not exists.\n", input_file.c_str());
      continue;
    }

    std::string entry_data;
    if (!mg::fs::read_file(input_file.c_str(), entry_data)) {
      continue;
    }

    count++;

    fprintf(stderr, "Adding file No. %d: %s\n", count, input_file.c_str());
    mzp.entry_headers.emplace_back();
    mzp.entry_data.emplace_back(entry_data);
  }

  // Write out the archive.
  std::string mzp_out;
  mg::data::mzp_write(mzp, mzp_out);

  if (!output_dir.empty() && !fs::is_directory(output_dir)) {
    if (!fs::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  if (!mg::fs::write_file(archives[0].c_str(), mzp_out)) {
    return -1;
  }

  fprintf(stderr, "Wrote %zu bytes from %d files to %s\n", mzp_out.size(),
                  count, archives[0].c_str());

  return 0;
}

int mzp_localize(std::vector<std::string> &paths,
                  std::vector<std::string> &archives) {
// Handle creating script_text.mrg.
  if (archives.size() > 1) {
    fprintf(stderr, "Specify only one [script_text.mrg].\n");
    return -1;
  }

  // Expand paths if necessary.
  fs::path input_dir;
  std::vector<std::string> input_files;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_files = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && fs::is_directory(paths[0])) {
      input_dir = paths[0];
    } else {
      input_files = paths;
    }
  } else {
    input_dir = fs::current_path() / "script_text";
    if (!fs::is_directory(input_dir)) {
      fprintf(stderr, "Input directory %s does not exist.\n",
                      input_dir.string().c_str());
      return -1;
    }
  }

  if (!input_dir.empty()) {
    // Apply *.txt pattern for directory.
    std::string pattern = input_dir.string() + "/*.txt";
    input_files = expand_glob_pattern(pattern);
  }

  if (input_files.empty()) {
    fprintf(stderr, "No files found for creating script_text.mrg.\n");
    return -1;
  }

  fs::path archive_path;
  if (archives.empty()) {
      archive_path = fs::current_path() / "script_text.mrg";
  } else if (isPotentialDirectory(archives[0])) {
    archive_path = fs::path(archives[0]) / "script_text.mrg";
  } else {
    archive_path = archives[0];
  }

  fs::path output_dir = archive_path.parent_path();

  // Initialize Mzp archive
  mg::data::Mzp mzp;

  // Process all .txt files in input folder
  for (const auto &input_file : input_files) {

    if (!fs::is_regular_file(input_file)) {
      fprintf(stderr, "File: %s does not exists.\n", input_file.c_str());
      continue;
    }

    fprintf(stderr, "Processing: %s\n", input_file.c_str());

    // Read file content
    std::string content;
    if (!mg::fs::read_file(input_file.c_str(), content)) {
      fprintf(stderr, "Skipping empty or invalid file: %s\n",
                      input_file.c_str());
      continue;
    }

    // Process text file to get offset and string tables
    auto [offset_table, string_table] = mg::data::make_table(content);
    if (offset_table.empty() || string_table.empty()) {
      fprintf(stderr, "Skipping empty or invalid file: %s\n",
                      input_file.c_str());
      continue;
    }

    // Add tables to Mzp archive
    mzp.add_entry(offset_table);
    mzp.add_entry(string_table);
  }

  // Write Mzp archive to string
  std::string mzp_data;
  mg::data::mzp_write(mzp, mzp_data);

  if (!output_dir.empty() && !fs::is_directory(output_dir)) {
    if (!fs::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  // Save to file
  if (!mg::fs::write_file(archive_path.string().c_str(), mzp_data)) {
    return 1;
  }

  fprintf(stderr, "Created %s\n", archive_path.string().c_str());
  return 0;
}

int mzx_decompress(std::vector<std::string> &archives,
                      std::vector<std::string> &paths) {
  // Handle decompression MZX files.

  if (archives.empty()) {
    fprintf(stderr, "MZX files not specified for decompress.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No MZX files found for decompression.\n");
    return -1;
  }

  fs::path output_dir;

  if (!paths.empty()) {
    if (paths.size() > 1) {
      fprintf(stderr, "Specify only one one output file "
                      "or directory for them.\n");
      return -1;
    }
    if (!isPotentialDirectory(paths[0]) && archive_files.size() > 1) {
      fprintf(stderr, "Specify only one MZX file when "
                      "specifying output file.\n");
      return -1;
    }

    if (isPotentialDirectory(paths[0])) {
      output_dir = paths[0];
    } else {
      output_dir = fs::path(paths[0]).parent_path();
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    if (!paths.empty()) {
      if (isPotentialDirectory(paths[0])) {
        output_path = output_dir / (fs::path(archive).stem().string() + ".raw");
      } else {
        output_path = paths[0];
      }
    } else {
      output_path = fs::path(archive).parent_path() /
                    (fs::path(archive).stem().string() + ".raw");
    }

    std::string src;
    if (!mg::fs::read_file(archive.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::mzx_decompress(src, dest, mzx_invert)) {
      fprintf(stderr, "Failed to decompress %s\n", archive.c_str());
      continue;
    }

    if (!mg::fs::write_file(output_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Decompressed %s to %s\n", archive.c_str(),
                    output_path.string().c_str());
  }

  return 0;
}

int mzx_compress(std::vector<std::string> &paths,
                  std::vector<std::string> &archives) {
  // Handle MZX compression.
  if (paths.empty()) {
    fprintf(stderr, "No files specified for compressing to MZX.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No files found to compress to MZX.\n");
    return -1;
  }

  fs::path archives_dir;

  if (!archives.empty()) {
    if (archives.size() > 1) {
      fprintf(stderr, "Specify in --archives only one "
                      "MZX file or directory for them.\n");
      return -1;
    }
    if (input_paths.size() > 1 && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify only one input file when "
                      "specifying a MZX file.\n");
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archives_dir = archives[0];
    } else {
      archives_dir = fs::path(archives[0]).parent_path();
    }

    if (!archives_dir.empty() && !fs::is_directory(archives_dir)) {
      if (!fs::create_directories(archives_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        archives_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path archive_path;

  for (const auto &path : input_paths) {

    if (!fs::is_regular_file(path)) {
      fprintf(stderr, "%s does not exist.\n", path.c_str());
      continue;
    }

    if (archives.empty()) {
      archive_path = fs::path(path).parent_path() /
                     (fs::path(path).stem().string() + ".bin");
    } else if (isPotentialDirectory(archives[0])) {
      archive_path = archives_dir / (fs::path(path).stem().string() + ".bin");
    } else {
      archive_path = archives[0];
    }

    std::string src;
    if (!mg::fs::read_file(path.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::mzx_compress(src, dest, 2, mzx_invert)) {
      fprintf(stderr, "Failed to compress %s\n", path.c_str());
      continue;
    }

    if (!mg::fs::write_file(archive_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Compressed %s to %s\n", path.c_str(),
                    archive_path.string().c_str());
  }

  return 0;
}

int mrg_info(std::vector<std::string> &archives) {
  // Handle information for MRG archive.
  if (archives.size() != 1) {
    fprintf(stderr, "Specify only one MRG archive "
                    "to get information about it.\n");
    return -1;
  }

  std::string input_basename = (fs::path(archives[0]).parent_path() /
                                fs::path(archives[0]).stem()).string();

  if (!fs::is_regular_file(input_basename + ".hed")) {
    fprintf(stderr, "%s does not exist.\n", archives[0].c_str());
    return -1;
  }

  // Test for input files.
  const std::string hed_filename =
      mg::string::format("%s.hed", input_basename.c_str());
  const std::string mrg_filename =
      mg::string::format("%s.mrg", input_basename.c_str());

  // Read raw data for hed files.
  std::string hed_raw;
  if (!mg::fs::read_file(hed_filename.c_str(), hed_raw)) {
    return -1;
  }

  // Try and map the mrg data.
  std::shared_ptr<mg::fs::MappedFile> mrg_data =
      mg::fs::MappedFile::open(mrg_filename.c_str());
  if (mrg_data == nullptr) {
    return -1;
  }

  // Try and read the NAM table as well. If we can't that's OK.
  const std::string nam_filename =
      mg::string::format("%s.nam", input_basename.c_str());
  std::string nam_raw;
  mg::data::Nam nam;
  const bool has_nam = mg::fs::read_file(nam_filename.c_str(), nam_raw) &&
                        nam_read(nam_raw, nam);

  // Parse the MRG data.
  auto mrg = mg::data::MappedMrg::parse(hed_raw, mrg_data);
  if (mrg == nullptr) {
    return -1;
  }

  // If we have a NAM and MRG, assert that
  // the filename count matches the entry count.
  if (has_nam && mrg->entries().size() != nam.names.size()) {
    fprintf(stderr, "MRG entry count (%zu) does not match "
                    "NAM entry count (%zu).\n",
                    mrg->entries().size(), nam.names.size());
    return -1;
  }

  // Print some info.
  for (unsigned i = 0; i < mrg->entries().size(); i++) {
    const std::string name_info = !has_nam ? "" :
        mg::string::format(", Name: '%s'", nam.names[i].c_str());
    const bool is_compressed = mrg->entries()[i].size_sectors !=
                               mrg->entries()[i].size_uncompressed_sectors;
    const std::string compress_info = !is_compressed ? "" :
        mg::string::format(", Uncompressed size 0x%08x sectors (< %u bytes)",
                            mrg->entries()[i].size_uncompressed_sectors,
                            mrg->entries()[i].size_uncompressed_sectors *
                            0x800);
    if (mrg_csv) {
      printf("%u,0x%08x,0x%08x,0x%08x,%s\n", i, mrg->entries()[i].offset,
              mrg->entries()[i].size_sectors,
              mrg->entries()[i].size_uncompressed_sectors,
              has_nam ? nam.names[i].c_str() : "");
    } else {
      printf("Entry %8u: Offset 0x%08x, Size 0x%08x sectors "
              "(%zu bytes)%s%s\n", i, mrg->entries()[i].offset,
              mrg->entries()[i].size_sectors, mrg->entry_data(i).size(),
              compress_info.c_str(), name_info.c_str());
    }
  }

  return 0;
}

int mrg_extract(std::vector<std::string> &archives,
                    std::vector<std::string> &paths) {
  // Handle extracting files from MRG archives.

  if (archives.empty()) {
    fprintf(stderr, "MRG archives not specified for extracting.\n");
    return -1;
  }

  if (!paths.empty() && !isPotentialDirectory(paths[0])) {
    fprintf(stderr, "Specify a directory for extract MRG archives.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else if (archives.size() == 1 && fs::is_directory(archives[0])) {
    // Apply *.hed pattern for directory.
    std::string pattern = archives[0] + "/*.hed";
    archive_files = expand_glob_pattern(pattern);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No .hed files found to extract MRG archives.\n");
    return -1;
  }

  fs::path output_dir, output_path;

  for (const auto &archive : archive_files) {

    std::string input_basename = (fs::path(archive).parent_path() /
                                  fs::path(archive).stem()).string();

    if (!fs::is_regular_file(input_basename + ".hed")) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    // Test for input files.
    const std::string hed_filename =
        mg::string::format("%s.hed", input_basename.c_str());
    const std::string mrg_filename =
        mg::string::format("%s.mrg", input_basename.c_str());

    // Read raw data for hed files.
    std::string hed_raw;
    if (!mg::fs::read_file(hed_filename.c_str(), hed_raw)) {
      return -1;
    }

    // Try and map the mrg data.
    std::shared_ptr<mg::fs::MappedFile> mrg_data =
        mg::fs::MappedFile::open(mrg_filename.c_str());
    if (mrg_data == nullptr) {
      return -1;
    }

    // Try and read the NAM table as well. If we can't that's OK.
    const std::string nam_filename =
        mg::string::format("%s.nam", input_basename.c_str());
    std::string nam_raw;
    mg::data::Nam nam;
    const bool has_nam = mg::fs::read_file(nam_filename.c_str(), nam_raw) &&
                          nam_read(nam_raw, nam);

    // Parse the MRG data.
    auto mrg = mg::data::MappedMrg::parse(hed_raw, mrg_data);
    if (mrg == nullptr) {
      return -1;
    }

    // If we have a NAM and MRG, assert that
    // the filename count matches the entry count.
    if (has_nam && mrg->entries().size() != nam.names.size()) {
      fprintf(stderr, "MRG entry count (%zu) does not match "
                      "NAM entry count (%zu).\n",
                      mrg->entries().size(), nam.names.size());
      return -1;
    }

    output_dir = paths.empty() ? fs::path(input_basename).parent_path() :
                 fs::path(paths[0]);

    // Ensure output dir exists.
    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create output path %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }

    // Utility method to write an index to an output file.
    std::string output_basename =
        (fs::path(input_basename)).stem().string();

    auto write_entry = [&](unsigned index) -> int {
      std::string output_filename =
          mg::string::format("%s.%08lu.dat", output_basename.c_str(), index);

      // If we have a name table, use that name as well.
      if (has_nam) {
        output_filename =
            mg::string::format("%s.%08lu.%s.dat", output_basename.c_str(),
                                index, nam.names[index].c_str());
      }

      auto entry_data = mrg->entry_data(index);
      output_path = output_dir;
      output_path.append(output_filename);
      if (!mg::fs::write_file(output_path.string().c_str(), entry_data)) {
        return -1;
      }
      fprintf(stderr, "Wrote %zu bytes to %s\n", entry_data.size(),
                      output_path.string().c_str());

      return 0;
    };

    // Iterate the mrg entries and emit.
    if (indexes.empty()) {
      for (std::vector<std::string>::size_type i = 0; i < mrg->entries().size();
          i++) {
        write_entry(i);
      }
    } else {
      for (long i : indexes) {
        if (i >= 0 && static_cast<std::vector<std::string>::size_type>(i) <
            mrg->entries().size()) {
          write_entry(i);
        }
      }
    }
  }

  return 0;
}

int mrg_pack(std::vector<std::string> &archives,
                std::vector<std::string> &paths,
                std::vector<std::string> &names) {
  // Handle packing MRG archive.

  if (archives.size() != 1) {
    fprintf(stderr, "Specify only one MRG archive for packing.\n");
    return -1;
  }

  if (!names.empty() && names.size() > 1) {
    fprintf(stderr, "Specify only one file with names for pack MRG archive.\n");
    return -1;
  }

  // Expand paths if necessary.
  fs::path input_dir;
  std::vector<std::string> input_files;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_files = expand_glob_pattern(paths[0]);
    } else if (paths.size() == 1 && fs::is_directory(paths[0])) {
      input_dir = paths[0];
    } else {
      input_files = paths;
    }
  } else {
    input_dir = fs::path(archives[0]).parent_path() /
                fs::path(archives[0]).stem();
  }

  if (!input_dir.empty()) {
    // Apply *.dat pattern for directory.
    std::string pattern = input_dir.string() + "/*.dat";
    input_files = expand_glob_pattern(pattern);
  }

  if (input_files.empty()) {
    fprintf(stderr, "No files found to pack in MRG archive.\n");
    return -1;
  }

  fs::path output_dir = fs::path(archives[0]).parent_path();

  if (!output_dir.empty() && !fs::is_directory(output_dir)) {
    if (!fs::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  std::string output_basename = (fs::path(archives[0]).parent_path() /
                                fs::path(archives[0]).stem()).string();

  // Read in each source file to a MRG entry.
  mg::data::Mrg mrg;
  for (const auto &input : input_files) {

    std::string data;
    if (!mg::fs::read_file(input.c_str(), data)) {
      continue;
    }

    // Attempt to detect certain compressed formats, so that we can put the
    // correct decompressed size in the hed.
    bool compressed = false;
    uint64_t decompressed_size = -1;

    // Nxx?
    {
      mg::data::Nxx nxx_header;
      if (extract_nxx_header(data, nxx_header)) {
        compressed = true;
        decompressed_size = nxx_header.size;
      }
    }

    mrg.entries.emplace_back(data, compressed, decompressed_size);
  }

  // If we were given a name file, create a NAM file as well.
  mg::data::Nam nam;
  if (!names.empty()) {
    if (!fs::is_regular_file(names[0])) {
      fprintf(stderr, "File with names %s does not exist.\n", names[0].c_str());
      return -1;
    }
    std::string name_data;
    if (!mg::fs::read_file(names[0].c_str(), name_data)) {
      return -1;
    }

    // Split the name input file on newline to get a list of names.
    std::stringstream ss(name_data);
    std::string line;
    while (std::getline(ss, line, '\n')) {
      nam.names.emplace_back(line);
    }
  }

  // Serialize mrg/hed.
  std::string mrg_out, hed_out;
  if (!mg::data::mrg_write(mrg, hed_out, mrg_out)) {
    fprintf(stderr, "Failed to pack MRG to %s\n", output_basename.c_str());
    return -1;
  }

  // Write outputs.
  std::string hed_filename =
      mg::string::format("%s.hed", output_basename.c_str());
  std::string mrg_filename =
      mg::string::format("%s.mrg", output_basename.c_str());

  if (!mg::fs::write_file(hed_filename.c_str(), hed_out)) {
    return -1;
  }
  if (!mg::fs::write_file(mrg_filename.c_str(), mrg_out)) {
    return -1;
  }

  // Serialize nam if given.
  std::string nam_out;
  if (nam.names.size() != 0) {
    if (!mg::data::nam_write(nam, nam_out)) {
      fprintf(stderr, "Failed to serialize NAM.\n");
      return -1;
    }

    std::string nam_filename =
        mg::string::format("%s.nam", output_basename.c_str());
    if (!mg::fs::write_file(nam_filename.c_str(), nam_out)) {
      return -1;
    }
  }

  return 0;
}

int mrg_replace(std::vector<std::string> &archives,
                  std::vector<std::string> &paths,
                  std::string &template_arc,
                  std::vector<std::string> &names) {
  // Handle replacement of entries in MRG archive.

  if (template_arc.empty()) {
    fprintf(stderr, "Specify in --template the source HED/MRG/NAM "
                    "archive for replacement files.\n");
    return -1;
  }

  if (archives.size() != 1) {
    fprintf(stderr, "Specify in --archives only one path to a new"
                    "HED/MRG/NAM archive with replaced entries.\n");
    return -1;
  }

  if (indexes.empty()) {
    fprintf(stderr, "Specify in --indexes the indexes of the files "
                    "in the HED/MRG/NAM archive for replacement.\n");
    return -1;
  }

  if (names.size() == 1) {
    paths.clear();
    if (!paths_from_file(names[0].c_str(), paths)) {
      return -1;
    }
  }

  if (paths.empty()) {
    fprintf(stderr, "Specify in --paths the paths to the files or in"
                    "--names path to file with paths to files "
                    "for replacement in the HED/MRG/NAM archive.\n");
    return -1;
  }

  if (indexes.size() != paths.size()) {
    fprintf(stderr, "The number of indexes %zu does not match the number of "
                    "file paths %zu for replacement in the HED/MRG/NAM "
                    "archive.\n", indexes.size(), paths.size());
    return -1;
  }

  std::string input_basename = (fs::path(template_arc).parent_path() /
                                fs::path(template_arc).stem()).string();

  std::string output_basename = (fs::path(archives[0]).parent_path() /
                                fs::path(archives[0]).stem()).string();

  if (output_basename == input_basename) {
    fprintf(stderr, "Specify in --archives a different path "
                    "for a new HED/MRG archive.\n");
    return -1;
  }

  if (!fs::is_regular_file(input_basename + ".hed")) {
    fprintf(stderr, "The source HED/MRG archive %s does not exist.\n",
                    (input_basename + ".hed").c_str());
    return -1;
  }

  std::set<std::string, NaturalCompare> filenames(paths.begin(), paths.end());
  std::map<long, const char*> replace_indices;
  auto idx_it = indexes.begin();
  auto file_it = filenames.begin();
  for (; idx_it != indexes.end() && file_it != filenames.end();
        ++idx_it, ++file_it) {
    std::string filename = fs::path(*file_it).filename().string();
    size_t dot = filename.find('.');
    size_t next_dot = filename.find('.', dot + 1);
    std::string num_part = (next_dot == std::string::npos) ?
                            filename.substr(dot + 1) :
                            filename.substr(dot + 1, next_dot - dot - 1);
    try {
        long file_idx = std::stol(num_part);
        if (file_idx != *idx_it) {
          fprintf(stderr, "Index %ld does not match number part "
                          "'%s' in filename '%s'.\n", *idx_it,
                          num_part.c_str(), filename.c_str());
          return -1;
        }
    } catch (const std::exception &e) {
      fprintf(stderr, "Error converting number part '%s' in '%s': %s\n",
                      num_part.c_str(), filename.c_str(), e.what());
      return -1;
    }

    replace_indices[*idx_it] = file_it->c_str();
  }

  // Load each of the replace files.
  std::map<long, std::unique_ptr<mg::fs::MappedFile>> replacement_files;
  for (auto &[index, filename] : replace_indices) {
    auto mapped = mg::fs::MappedFile::open(filename);
    if (mapped == nullptr) {
      return -1;
    }
    replacement_files[index] = std::move(mapped);
  }

  // Check for the original mrg/hed files
  const std::string input_hed_filename =
      mg::string::format("%s.hed", input_basename.c_str());
  const std::string input_mrg_filename =
      mg::string::format("%s.mrg", input_basename.c_str());

  // Read raw data for hed file.
  std::string hed_raw;
  if (!mg::fs::read_file(input_hed_filename.c_str(), hed_raw)) {
    return -1;
  }

  // Try and map the mrg data.
  std::shared_ptr<mg::fs::MappedFile> mrg_data =
      mg::fs::MappedFile::open(input_mrg_filename.c_str());
  if (mrg_data == nullptr) {
    return -1;
  }

  // Parse the MRG data.
  auto mrg = mg::data::MappedMrg::parse(hed_raw, mrg_data);
  if (mrg == nullptr) {
    return -1;
  }

  fs::path output_dir = fs::path(output_basename).parent_path();

  if (!output_dir.empty() && !fs::is_directory(output_dir)) {
    if (!fs::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  // Make output names.
  const std::string output_hed_filename =
      mg::string::format("%s.hed", output_basename.c_str());
  const std::string output_mrg_filename =
      mg::string::format("%s.mrg", output_basename.c_str());

  // Open hed output.
  const int hed_fd = open(output_hed_filename.c_str(),
                          O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (hed_fd == -1) {
    fprintf(stderr, "Failed to open '%s' - %s\n", output_hed_filename.c_str(),
                    strerror(errno));
    return -1;
  }
  std::shared_ptr<void> _defer_close_hed_fd(nullptr,
                                            [=](...) { close(hed_fd); });

  // Open mrg output.
  const int mrg_fd = open(output_mrg_filename.c_str(),
                          O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (mrg_fd == -1) {
    fprintf(stderr, "Failed to open '%s' - %s\n", output_mrg_filename.c_str(),
                    strerror(errno));
    return -1;
  }
  std::shared_ptr<void> _defer_close_mrg_fd(nullptr,
                                            [=](...) { close(mrg_fd); });

  // Padding buffer for rounding to the nearest segment.
  char padding[mg::data::Mrg::SECTOR_SIZE];
  memset(padding, 0x0, sizeof(padding));

  // Generate our new hed/mrg files.
  ssize_t mrg_write_offset = 0;

  fprintf(stderr, "Replace %zu files in %s\n", replace_indices.size(),
                   output_mrg_filename.c_str());
  for (unsigned i = 0; i < mrg->entries().size(); i++) {

    // Create the header for this next entry.
    // Offset must always be the new current mrg offset.
    mg::data::Mrg::PackedEntryHeader header;
    header.offset = mrg_write_offset / mg::data::Mrg::SECTOR_SIZE;

    // Do we want to replace this index?
    const bool replace_index = replace_indices.find(i) != replace_indices.end();

    // Calculate the file size in header.
    if (replace_index) {
      // Use size / data from the replacement table.
      const auto &replacement_data = replacement_files.at(i);
      header.size_sectors =
          mg::data::Mrg::size_in_sectors(replacement_data->size());
      header.size_uncompressed_sectors = header.size_sectors;

      // If this is a known compress format,
      // update the decompressed sector size.
      {
        mg::data::Nxx nxx_header;
        if (extract_nxx_header(replacement_data->string_view(), nxx_header)) {
          header.size_uncompressed_sectors =
              mg::data::Mrg::size_in_sectors(nxx_header.size);
        }
      }
    } else {
      // Use the sizes / data from the old header.
      const auto &old_header = mrg->entries()[i];
      header.size_sectors = old_header.size_sectors;
      header.size_uncompressed_sectors = old_header.size_uncompressed_sectors;
    }

    // Write the data segment.
    if (replace_index) {
      // Write the new data.
      const auto &replacement_data = replacement_files.at(i);
      ASSERT(write(mrg_fd, replacement_data->data(),
                   replacement_data->size()) == replacement_data->size());
      mrg_write_offset += replacement_data->size();

      // Pad to the nearest sector.
      const ssize_t bytes_to_pad = (header.size_sectors *
          mg::data::Mrg::SECTOR_SIZE) - replacement_data->size();
      ASSERT(write(mrg_fd, padding, bytes_to_pad) == bytes_to_pad);
      mrg_write_offset += bytes_to_pad;
    } else {
      const auto old_data = mrg->entry_data(i);
      ASSERT(write(mrg_fd, old_data.data(), old_data.size()) ==
             (ssize_t)old_data.size());
      mrg_write_offset += old_data.size();
    }

    // Write the hew HED entry.
    header.to_file_order();
    ASSERT(write(hed_fd, &header, sizeof(header)) == sizeof(header));
  }

  // Write two all-F HED entries to indicate EOF.
  char eof[sizeof(mg::data::Mrg::PackedEntryHeader) * 2];
  memset(eof, 0xFF, sizeof(eof));
  ASSERT(write(hed_fd, eof, sizeof(eof)) == sizeof(eof));

  return 0;
}

int nxx_decompress(std::vector<std::string> &archives,
                      std::vector<std::string> &paths) {

  // Handle decompression NX files.

  if (archives.empty()) {
    fprintf(stderr, "NX files not specified for decompress.\n");
    return -1;
  }

  // Expand archive if it's a glob pattern or directory.
  std::vector<std::string> archive_files;
  if (archives.size() == 1 && is_glob_pattern(archives[0])) {
    archive_files = expand_glob_pattern(archives[0]);
  } else {
    archive_files = archives;
  }

  if (archive_files.empty()) {
    fprintf(stderr, "No NX files found for decompression.\n");
    return -1;
  }

  fs::path output_dir;

  if (!paths.empty()) {
    if (paths.size() > 1) {
      fprintf(stderr, "Specify only one one output file "
                      "or directory for them.\n");
      return -1;
    }
    if (!isPotentialDirectory(paths[0]) && archive_files.size() > 1) {
      fprintf(stderr, "Specify only one NX file when "
                      "specifying output file.\n");
      return -1;
    }

    if (isPotentialDirectory(paths[0])) {
      output_dir = paths[0];
    } else {
      output_dir = fs::path(paths[0]).parent_path();
    }

    if (!output_dir.empty() && !fs::is_directory(output_dir)) {
      if (!fs::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path output_path;

  for (const auto &archive : archive_files) {

    if (!fs::is_regular_file(archive)) {
      fprintf(stderr, "%s does not exist.\n", archive.c_str());
      continue;
    }

    if (!paths.empty()) {
      if (isPotentialDirectory(paths[0])) {
        output_path = output_dir /
                      (fs::path(archive).stem().string() + ".bntx");
      } else {
        output_path = paths[0];
      }
    } else {
      output_path = fs::path(archive).parent_path() /
                    (fs::path(archive).stem().string() + ".bntx");
    }

    std::string src;
    if (!mg::fs::read_file(archive.c_str(), src)) {
      return -1;
    }

    std::string dest;
    if (!mg::data::nxx_decompress(src, dest)) {
      fprintf(stderr, "Failed to decompress %s\n", archive.c_str());
      return -1;
    }

    if (!mg::fs::write_file(output_path.string().c_str(), dest)) {
      return -1;
    }

    fprintf(stderr, "Decompressed %s to %s\n", archive.c_str(),
                    output_path.string().c_str());
  }

  return 0;
}

int nxgx_compress(std::vector<std::string> &paths,
                  std::vector<std::string> &archives) {
  // Handle NXGX (NXGZ) compression.
  if (paths.empty()) {
    fprintf(stderr, "No files specified for compressing to NXGX.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No files found to compress to NXGX.\n");
    return -1;
  }

  fs::path archives_dir;

  if (!archives.empty()) {
    if (archives.size() > 1) {
      fprintf(stderr, "Specify in --archives only one "
                      "NXGX file or directory for them.\n");
      return -1;
    }
    if (input_paths.size() > 1 && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify only one input file when "
                      "specifying a NXGX file.\n");
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archives_dir = archives[0];
    } else {
      archives_dir = fs::path(archives[0]).parent_path();
    }

    if (!archives_dir.empty() && !fs::is_directory(archives_dir)) {
      if (!fs::create_directories(archives_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        archives_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path archive_path;

  for (const auto &path : input_paths) {

    if (!fs::is_regular_file(path)) {
      fprintf(stderr, "%s does not exist.\n", path.c_str());
      continue;
    }

    if (archives.empty()) {
      archive_path = fs::path(path).parent_path() /
                     (fs::path(path).stem().string() + ".dat");
    } else if (isPotentialDirectory(archives[0])) {
      archive_path = archives_dir / (fs::path(path).stem().string() + ".dat");
    } else {
      archive_path = archives[0];
    }

    std::string src;
    if (!mg::fs::read_file(path.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::nxgx_compress(src, dest)) {
      fprintf(stderr, "Failed to compress %s\n", path.c_str());
      continue;
    }

    if (!mg::fs::write_file(archive_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Compressed %s to %s\n", path.c_str(),
                    archive_path.string().c_str());
  }

  return 0;
}

int nxcx_compress(std::vector<std::string> &paths,
                  std::vector<std::string> &archives) {
  // Handle NXCX (NXZ) compression.
  if (paths.empty()) {
    fprintf(stderr, "No files specified for compressing to NXCX.\n");
    return -1;
  }

  // Expand paths if necessary.
  std::vector<std::string> input_paths;
  if (!paths.empty()) {
    if (paths.size() == 1 && is_glob_pattern(paths[0])) {
      input_paths = expand_glob_pattern(paths[0]);
    } else {
      input_paths = paths;
    }
  }

  if (input_paths.empty()) {
    fprintf(stderr, "No files found to compress to NXCX.\n");
    return -1;
  }

  fs::path archives_dir;

  if (!archives.empty()) {
    if (archives.size() > 1) {
      fprintf(stderr, "Specify in --archives only one "
                      "NXCX file or directory for them.\n");
      return -1;
    }
    if (input_paths.size() > 1 && !isPotentialDirectory(archives[0])) {
      fprintf(stderr, "Specify only one input file when "
                      "specifying a NXCX file.\n");
      return -1;
    }

    if (isPotentialDirectory(archives[0])) {
      archives_dir = archives[0];
    } else {
      archives_dir = fs::path(archives[0]).parent_path();
    }

    if (!archives_dir.empty() && !fs::is_directory(archives_dir)) {
      if (!fs::create_directories(archives_dir)) {
        fprintf(stderr, "Failed to create directory %s\n",
                        archives_dir.string().c_str());
        return -1;
      }
    }
  }

  fs::path archive_path;

  for (const auto &path : input_paths) {

    if (!fs::is_regular_file(path)) {
      fprintf(stderr, "%s does not exist.\n", path.c_str());
      continue;
    }

    if (archives.empty()) {
      archive_path = fs::path(path).parent_path() /
                     (fs::path(path).stem().string() + ".dat");
    } else if (isPotentialDirectory(archives[0])) {
      archive_path = archives_dir / (fs::path(path).stem().string() + ".dat");
    } else {
      archive_path = archives[0];
    }

    std::string src;
    if (!mg::fs::read_file(path.c_str(), src)) {
      continue;
    }

    std::string dest;
    if (!mg::data::nxcx_compress(src, dest)) {
      fprintf(stderr, "Failed to compress %s\n", path.c_str());
      continue;
    }

    if (!mg::fs::write_file(archive_path.string().c_str(), dest)) {
      continue;
    }

    fprintf(stderr, "Compressed %s to %s\n", path.c_str(),
                    archive_path.string().c_str());
  }

  return 0;
}

void usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [options]\n\n", program_name);
  fprintf(stderr, "Options:\n\n");
  fprintf(stderr, "  -h, --help                    "
                  "Show this help message and exit.\n");
  fprintf(stderr, "  -hl, --hfa_list               "
                  "Create lists of files in HFA archives\n"
                  "                                  "
                  "in a folder specified in --paths.\n");
  fprintf(stderr, "  -he, --hfa_extract            "
                  "Extract files from HFA archives.\n");
  fprintf(stderr, "  -ai, --as_is                  "
                  "Extract files from HFA archives in\n"
                  "                                  "
                  "their original form (as is) when\n"
                  "                                  "
                  "specifying a folder in --paths.\n");
  fprintf(stderr, "  -hi, --hfa_inject             "
                  "Inject files into HFA archive.\n");
  fprintf(stderr, "  -ha, --hfa_add                "
                  "Adding files to an existing or new\n"
                  "                                  "
                  "HFA archive. If specified in\n"
                  "                                  "
                  "--indexes, indexes will be sorted in\n"
                  "                                  "
                  "ascending order, and the paths in\n"
                  "                                  "
                  "--paths must match this order.\n");
  fprintf(stderr, "  -lz, --lenzu                  "
                  "Decompress lenzu files\n"
                  "                                  "
                  "(as .txt by default).\n");
  fprintf(stderr, "  -ct, --ccit_txt               "
                  "Convert CCIT to TXT table.\n"
                  "                                  "
                  "(After Lenzu decompress).\n");
  fprintf(stderr, "  -tc, --txt_ccit               "
                  "Convert TXT table to CCIT.\n");
  fprintf(stderr, "  -cp, --cbg_png                "
                  "Convert CBG to PNG.\n");
  fprintf(stderr, "  -pc, --png_cbg                "
                  "Convert PNG to CBG.\n");
  fprintf(stderr, "  -mp, --mzp_png                "
                  "Convert MzpImage archives to PNG.\n");
  fprintf(stderr, "  -pm, --png_mzp                "
                  "Convert PNG to MzpImage archive.\n");
  fprintf(stderr, "  -mzi, --mzp_info              "
                  "List information about the\n"
                  "                                  "
                  "existing MZP archive.\n");
  fprintf(stderr, "  -mze, --mzp_extract           "
                  "Extract .bin entries from\n"
                  "                                  "
                  "MZP archives to a folder.\n");
  fprintf(stderr, "  -mzp, --mzp_pack              "
                  "Combine .bin entries (or other files)\n"
                  "                                  "
                  "into ONE MZP archive.\n");
  fprintf(stderr, "  -mzl, --mzp_localize          "
                  "Creating a localization archive\n"
                  "                                  "
                  "(script_text.mrg) from localization\n"
                  "                                  "
                  "string files, for example,\n"
                  "                                  "
                  "named as follows:\n"
                  "                                  "
                  "01_loc_japanese.txt\n"
                  "                                  "
                  "02_loc_english.txt\n"
                  "                                  "
                  "03_loc_schinese.txt\n"
                  "                                  "
                  "04_loc_tchinese.txt\n"
                  "                                  "
                  "05_loc_dummy.txt\n");
  fprintf(stderr, "  -mxd, --mzx_decompress        "
                  "Decompress MZX files.\n");
  fprintf(stderr, "  -mxc, --mzx_compress          "
                  "Compress files to MZX.\n");
  fprintf(stderr, "  -mxi, --mzx_invert            "
                  "Invert bytes during decompression\n"
                  "                                  "
                  "from MZX or compression to MZX.\n");
  fprintf(stderr, "  -mri, --mrg_info              "
                  "Print the file list contained\n"
                  "                                  "
                  "in the HED/MRG/NAM archive.\n"
                  "                                  "
                  "Specify '--csv' to get output in\n"
                  "                                  "
                  "machine-readable format.\n");
  fprintf(stderr, "  --csv                         "
                  "Output of '--mrg_info' in machine\n"
                  "                                  "
                  "readable format (CSV).\n");
  fprintf(stderr, "  -mre, --mrg_extract           "
                  "Extract .dat entries from HED/MRG/NAM\n"
                  "                                  "
                  "archives to a folder.\n");
  fprintf(stderr, "  -mrp, --mrg_pack              "
                  "Construct a new HED/MRG/NAM archive\n"
                  "                                  "
                  "from individual files.\n");
  fprintf(stderr, "  -mrgr, --mrg_replace          "
                  "Replace entries in the ONE\n"
                  "                                  "
                  "HED/MRG/NAM archive. Indexes specified\n"
                  "                                  "
                  "in --indexes will be sorted in\n"
                  "                                  "
                  "ascending order, and the paths in\n"
                  "                                  "
                  "--paths must match this order.\n"
                  "                                  "
                  "Expected file name prefix of files in\n"
                  "                                  "
                  "the format arcname.00000000. where\n"
                  "                                  "
                  "00000000 is the eight-digit index of\n"
                  "                                  "
                  "the file in the archive.\n");
  fprintf(stderr, "  -nxd, --nxx_decompress        "
                  "Decompress NXX files.\n");
  fprintf(stderr, "  -nxgx, --nxgx_compress        "
                  "Compress files to NXGX (NXGZ).\n");
  fprintf(stderr, "  -nxcx, --nxcx_compress        "
                  "Compress files to NXCX (NXZ).\n");
  fprintf(stderr, "  -a, --archives <paths> ...    "
                  "Paths to archives or the folder\n"
                  "                                  "
                  "(without dots in the final folder)\n"
                  "                                  "
                  "containing them for processing.\n"
                  "                                  "
                  "(Supports glob pattern, e.g., *.hfa.)\n"
                  "                                  "
                  "(ONE archive for injection, if paths\n"
                  "                                  "
                  "to FILES are specified in --paths,\n"
                  "                                  "
                  "for information about MZP and\n"
                  "                                  "
                  "HED/MRG/NAM archives, for pack a\n"
                  "                                  "
                  "HED/MRG/NAM archive, and replace entries\n"
                  "                                  "
                  "of a HED/MRG/NAM archive.)\n");
  fprintf(stderr, "  -p, --paths <paths> ...       "
                  "Paths to a folder\n"
                  "                                  "
                  "(without dots in the final folder)\n"
                  "                                  "
                  "or files for output or injection.\n"
                  "                                  "
                  "(Supports glob pattern, e.g., *.png.)\n"
                  "                                  "
                  "(ONE file for injection, if a specified\n"
                  "                                  "
                  "path to a CBG, MZP, or MZX file\n"
                  "                                  "
                  "(not to a folder with them)\n"
                  "                                  "
                  "is provided in --archives.)\n");
  fprintf(stderr, "  -n, --names <names> ...       "
                  "Names of files (with extensions) for\n"
                  "                                  "
                  "extraction from ONE HFA archive.\n"
                  "                                  "
                  "(Supports glob pattern, e.g., *.ctd.)\n"
                  "                                  "
                  "For --mrg_pack, specify the path to\n"
                  "                                  "
                  "the file with names for the entries.\n"
                  "                                  "
                  "For --mrg_replace, you can specify\n"
                  "                                  "
                  "the path to the file containing paths\n"
                  "                                  "
                  "to the files that will be replaced\n"
                  "                                  "
                  "in the archive. If you specify a file\n"
                  "                                  "
                  "path, the paths in --paths\n"
                  "                                  "
                  "will be ignored.\n");
  fprintf(stderr, "  -i, --indexes <indexes> ...   "
                  "Indexes of entries for extraction from\n"
                  "                                  "
                  "ONE HFA or HED/MRG/NAM archive,\n"
                  "                                  "
                  "adding files in ONE HFA archive,\n"
                  "                                  "
                  "or for replacement in the ONE\n"
                  "                                  "
                  "HED/MRG/NAM archive.\n");
  fprintf(stderr, "  -t, --template <path>         "
                  "Path to the template archive or\n"
                  "                                  "
                  "folder containing template archives for\n"
                  "                                  "
                  "--hfa_inject, --png_cbg, --png_mzp and\n"
                  "                                  "
                  "--mrg_replace. If a template is specified,\n"
                  "                                  "
                  "you can provide a path to a new archive or\n"
                  "                                  "
                  "folder for new archives in --archives.\n");
}

void short_usage(const char *program_name) {
  fprintf(stderr, "Usage: %s [options]\n", program_name);
  fprintf(stderr, "Options: -h/--help, -hl/--hfa_list, -he/--hfa_extract, "
                  "-ai/--as_is,\n"
                  "         -hi/--hfa_inject, -ha/-hfa_add, -lz/--lenzu,\n"
                  "         -ct/--ccit_txt, -tc/--txt_ccit, -cp/--cbg_png, "
                  "-pc/--png_cbg,\n"
                  "         -mp/--mzp_png, -pm/--png_mzp, -mzi/--mzp_info, "
                  "-mze/--mzp_extract,\n"
                  "         -mzp/--mzp_pack, -mzl/--mzp_localize (create "
                  "localization archive),\n"
                  "         -mxd/--mzx_decompress, -mxc/--mzx_compress,"
                  " -mxi/--mzx_invert,\n"
                  "         -mri/--mrg_info, --csv, -mre/--mrg_extract, "
                  "-mrp/--mrg_pack,\n"
                  "         -mrgr/--mrg_replace, -nxd/--nxx_decompress,\n"
                  "         -nxgx/--nxgx_compress, -nxcx/--nxcx_compress,\n"
                  "         -a/--archives <paths>, -p/--paths <paths>, "
                  "-n/--names <names>, \n"
                  "         -i/--indexes <indexes>, -t/--template <path>\n");
}

int main(int argc, char **argv) {
  bool hfa_list = false, hfa_ext = false, hfa_inj = false, hfa_add = false;
  bool lenzu_dec = false, ccit_txt = false, txt_ccit = false;
  bool cbg_png = false, png_cbg = false, mzp_png = false, png_mzp = false;
  bool mzp_inf = false, mzp_ext = false, mzp_pac = false, mzp_loc = false;
  bool mzx_dec = false, mzx_comp = false;
  bool mrg_inf = false, mrg_ext = false;
  bool mrg_pac = false, mrg_rep = false;
  bool nxx_dec = false, nxgx_comp = false, nxcx_comp = false;
  std::vector<std::string> archives, paths, names;
  std::string template_arc;

  if (argc < 2) {
    usage(argv[0]);
    return -1;
  }

  // Parse arguments.
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (arg == "-hl" || arg == "--hfa_list") {
      hfa_list = true;
    } else if (arg == "-he" || arg == "--hfa_extract") {
      hfa_ext = true;
    } else if (arg == "-ai" || arg == "--as_is") {
      as_is = hfa_ext = true;
    } else if (arg == "-hi" || arg == "--hfa_inject") {
      hfa_inj = true;
    } else if (arg == "-ha" || arg == "--hfa_add") {
      hfa_add = true;
    } else if (arg == "-lz" || arg == "--lenzu") {
      lenzu_dec = true;
    } else if (arg == "-ct" || arg == "--ccit_txt") {
      ccit_txt = true;
    } else if (arg == "-tc" || arg == "--txt_ccit") {
      txt_ccit = true;
    } else if (arg == "-cp" || arg == "--cbg_png") {
      cbg_png = true;
    } else if (arg == "-pc" || arg == "--png_cbg") {
      png_cbg = true;
    } else if (arg == "-mp" || arg == "--mzp_png") {
      mzp_png = true;
    } else if (arg == "-pm" || arg == "--png_mzp") {
      png_mzp = true;
    } else if (arg == "-mzi" || arg == "--mzp_info") {
      mzp_inf = true;
    } else if (arg == "-mze" || arg == "--mzp_extract") {
      mzp_ext = true;
    } else if (arg == "-mzp" || arg == "--mzp_pack") {
      mzp_pac = true;
    } else if (arg == "-mzl" || arg == "--mzp_localize") {
      mzp_loc = true;
    } else if (arg == "-mxd" || arg == "--mzx_decompress") {
      mzx_dec = true;
    } else if (arg == "-mxc" || arg == "--mzx_compress") {
      mzx_comp = true;
    } else if (arg == "-mxi" || arg == "--mzx_invert") {
      mzx_invert = true;
    } else if (arg == "-mri" || arg == "--mrg_info") {
      mrg_inf = true;
    } else if (arg == "--csv") {
      mrg_inf = mrg_csv = true;
    } else if (arg == "-mre" || arg == "--mrg_extract") {
      mrg_ext = true;
    } else if (arg == "-mrp" || arg == "--mrg_pack") {
      mrg_pac = true;
    } else if (arg == "-mrgr" || arg == "--mrg_replace") {
      mrg_rep = true;
    } else if (arg == "-nxd" || arg == "--nxx_decompress") {
      nxx_dec = true;
    } else if (arg == "-nxgx" || arg == "--nxgx_compress") {
      nxgx_comp = true;
    } else if (arg == "-nxcx" || arg == "--nxcx_compress") {
      nxcx_comp = true;
    } else if (arg == "-a" || arg == "--archives") {
      while (++i < argc && argv[i][0] != '-') {
        archives.emplace_back(argv[i]);
      }
      --i;
    } else if (arg == "-p" || arg == "--paths") {
      while (++i < argc && argv[i][0] != '-') {
        paths.emplace_back(argv[i]);
      }
      --i;
    } else if (arg == "-n" || arg == "--names") {
      while (++i < argc && argv[i][0] != '-') {
        names.emplace_back(argv[i]);
      }
      --i;
    } else if (arg == "-i" || arg == "--indexes") {
      if (i + 1 >= argc || argv[i + 1][0] == '-') {
        short_usage(argv[0]);
        fprintf(stderr, "%s: error: missing values for indexes.\n", argv[0]);
        return -1;
      }
      while (++i < argc && argv[i][0] != '-') {
        char *endptr;
        long idx = strtol(argv[i], &endptr, 0);
        if (endptr == argv[i] || *endptr != '\0') {
          // Stop if the argument is not a valid number.
          break;
        }
        indexes.emplace(idx);
      }
      --i;
    } else if (arg == "-t" || arg == "--template") {
      if (++i >= argc) {
        short_usage(argv[0]);
        fprintf(stderr, "%s: error: missing value for --template.\n", argv[0]);
        return -1;
      }
      template_arc = argv[i];
    } else {
      short_usage(argv[0]);
      fprintf(stderr, "%s: error: unknown option: %s\n", argv[0], arg.c_str());
      return -1;
    }
  }

  // Handle operations.
  if (hfa_list) {
    return listing_hfa(archives, paths);
  }

  if (hfa_ext) {
    return hfa_extract(archives, names, paths);
  }

  if (hfa_inj) {
    return hfa_inject(archives, paths, template_arc);
  }

  if (hfa_add) {
    return hfa_add_files(archives, paths, template_arc);
  }

  if (lenzu_dec) {
    return lenzu_decompress(archives, paths);
  }

  if (ccit_txt) {
    return ccit_to_txt(archives, paths);
  }

  if (txt_ccit) {
    return txt_to_ccit(paths, archives);
  }

  if (cbg_png) {
    return cbg_to_png(archives, paths);
  }

  if (png_cbg) {
    return png_to_cbg(paths, archives, template_arc);
  }

  if (mzp_png) {
    return mzp_to_png(archives, paths);
  }

  if (png_mzp) {
    return png_to_mzp(paths, archives, template_arc);
  }

  if (mzp_inf) {
    return mzp_info(archives);
  }

  if (mzp_ext) {
    return mzp_extract(archives, paths);
  }

  if (mzp_pac) {
    return mzp_pack(paths, archives);
  }

  if (mzp_loc) {
    return mzp_localize(paths, archives);
  }

  if (mzx_dec) {
    return mzx_decompress(archives, paths);
  }

  if (mzx_comp) {
    return mzx_compress(paths, archives);
  }

  if (mrg_inf) {
    return mrg_info(archives);
  }

  if (mrg_ext) {
    return mrg_extract(archives, paths);
  }

  if (mrg_pac) {
    return mrg_pack(archives, paths, names);
  }

  if (mrg_rep) {
    return mrg_replace(archives, paths, template_arc, names);
  }

  if (nxx_dec) {
    return nxx_decompress(archives, paths);
  }

  if (nxgx_comp) {
    return nxgx_compress(paths, archives);
  }

  if (nxcx_comp) {
    return nxcx_compress(paths, archives);
  }

  return 0;
}
