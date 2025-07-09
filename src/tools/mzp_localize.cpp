#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
#include <mg/util/fs.hpp>
#include <mg/util/string.hpp>
#include <mg/data/mzp.hpp>
#include <mg/data/mzp_table_maker.hpp>

// Print usage information
void usage(const char *prog_name) {
  fprintf(stderr, "Usage: %s [-i|--input <path>] [-o|--output <path>]\n",
                  prog_name);
  fprintf(stderr, "  -i, --input   Folder with input localization files.\n");
  fprintf(stderr, "  -o, --output  Directory for output archive.\n");
}

int main(int argc, char **argv) {
  // Determine input and output directories
  std::filesystem::path cwd = std::filesystem::current_path();
  std::filesystem::path input_path = cwd / "script_text";
  std::filesystem::path output_path = cwd;

  // Manual argument parsing
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
      if (i + 1 >= argc) {
        usage(argv[0]);
        fprintf(stderr, "Error: --input requires an argument.\n");
        return -1;
      }
      input_path = argv[++i];
    } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
      if (i + 1 >= argc) {
        usage(argv[0]);
        fprintf(stderr, "Error: --output requires an argument.\n");
        return -1;
      }
      output_path = argv[++i];
    } else {
      usage(argv[0]);
      fprintf(stderr, "Error: Unknown argument '%s'\n", argv[i]);
      return -1;
    }
  }

  if (!std::filesystem::is_directory(input_path)) {
    usage(argv[0]);
    fprintf(stderr, "Input directory %s does not exist.\n",
                    input_path.string().c_str());
    return -1;
  }

  // Initialize Mzp archive
  mg::data::Mzp mzp;

  // Process all .txt files in input folder
  try {
    for (const auto &entry : std::filesystem::directory_iterator(input_path)) {
      if (entry.path().extension() != ".txt") continue;

      std::string file_path = entry.path().string();
      fprintf(stderr, "Processing: %s\n", file_path.c_str());

      // Read file content
      std::string content;
      if (!mg::fs::read_file(file_path.c_str(), content)) {
        fprintf(stderr, "Skipping empty or invalid file: %s\n",
                        file_path.c_str());
        continue;
      }

      // Process text file to get offset and string tables
      auto [offset_table, string_table] = mg::data::make_table(content);
      if (offset_table.empty() || string_table.empty()) {
        fprintf(stderr, "Skipping empty or invalid file: %s\n",
                        file_path.c_str());
        continue;
      }

      // Add tables to Mzp archive
      mzp.add_entry(offset_table);
      mzp.add_entry(string_table);
    }
  } catch (const std::filesystem::filesystem_error &e) {
    fprintf(stderr, "Filesystem error: %s\n", e.what());
    return -1;
  }

  // Generate output filename with timestamp
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);
  std::string output_file = (output_path / mg::string::format(
      "script_text_%04d%02d%02d-%02d%02d%02d.mrg",
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
      tm.tm_hour, tm.tm_min, tm.tm_sec)).string();

  // Write Mzp archive to string
  std::string mzp_data;
  mg::data::mzp_write(mzp, mzp_data);

  if (!output_path.empty() && !std::filesystem::is_directory(output_path)) {
    if (!std::filesystem::create_directories(output_path)) {
      fprintf(stderr, "Failed to create directory: %s\n",
                      output_path.string().c_str());
      return -1;
    }
  }

  // Save to file
  if (!mg::fs::write_file(output_file.c_str(), mzp_data)) {
    return -1;
  }

  fprintf(stderr, "Created %s\n", output_file.c_str());
  return 0;
}