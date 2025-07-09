#include <filesystem>
#include <fstream>
#include <regex>
#include <mg/data/mzp.hpp>
#include <mg/util/fs.hpp>
#include <mg/util/string.hpp>

namespace fs = std::filesystem;

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

bool is_glob_pattern(const std::string &pattern) {
  return pattern.find('*') != std::string::npos ||
            pattern.find('?') != std::string::npos;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s output input [input..]\n"
                    "  or: %s output @filelist.txt "
                    "(to read files from a list)\n"
                    "  or: %s output '*.txt' (to use a glob pattern)\n",
        argv[0], argv[0], argv[0]);
    return -1;
  }

  std::vector<std::string> input_files;

  if (argc == 3) {
    std::string arg = argv[2];

    if (arg.size() > 0 && arg[0] == '@') {
      std::string filelist_path = arg.substr(1);
      std::ifstream filelist(filelist_path);
      if (!filelist) {
        fprintf(stderr, "Failed to open file list: %s\n",
                filelist_path.c_str());
        return -1;
      }

      std::string line;
      while (std::getline(filelist, line)) {
        input_files.push_back(line);
      }
    } else if (is_glob_pattern(arg)) {
      fs::path glob_path(arg);
      fs::path dir = glob_path.has_parent_path() ?
                        glob_path.parent_path() : ".";
      std::string filename_pattern = glob_path.filename().string();
      std::string regex_pattern = glob_to_regex(filename_pattern);
      std::regex regex_pattern_compiled(regex_pattern,
                                        std::regex_constants::icase);

      for (const auto &entry : fs::directory_iterator(dir)) {
        if (std::regex_match(entry.path().filename().string(),
                                    regex_pattern_compiled)) {
          input_files.push_back(entry.path().string());
        }
      }
    } else {
      if (!fs::is_regular_file(arg)) {
        fprintf(stderr, "File '%s' does not exist.\n", arg.c_str());
        return -1;
      }
      input_files.push_back(arg);
    }
  } else {
    for (int i = 2; i < argc; i++) {
      if (!fs::is_regular_file(argv[i])) {
        fprintf(stderr, "File '%s' does not exist.\n", argv[i]);
        continue;
      }
      input_files.push_back(argv[i]);
    }
  }

  // Construct new MZP
  mg::data::Mzp mzp;

  // Read each input file as a new MZP record
  for (const auto &input_file : input_files) {
    std::string entry_data;
    if (!mg::fs::read_file(input_file.c_str(), entry_data)) {
      return -1;
    }

    fprintf(stderr, "Adding file %s\n", input_file.c_str());
    mzp.entry_headers.emplace_back();
    mzp.entry_data.emplace_back(entry_data);
  }

  // Write out the archive
  std::string mzp_out;
  mg::data::mzp_write(mzp, mzp_out);
  if (!mg::fs::write_file(argv[1], mzp_out)) {
    return -1;
  }

  fs::path output_path(argv[1]);
  fs::path output_dir = output_path.parent_path();
  if (!output_dir.empty() && !fs::is_directory(output_dir)) {
    if (!fs::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory: %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  fprintf(stderr, "Wrote %zu bytes to %s\n", mzp_out.size(), argv[1]);

  return 0;
}
