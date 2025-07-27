
#include <cstring>
#include <string>
#include <filesystem>

#include <mg/data/ccit.hpp>
#include <mg/util/fs.hpp>

void usage(const char *prog_name) {
  fprintf(stderr, "Usage: %s [-h] [-c] [-p PATH] file\n", prog_name);
  fprintf(stderr, "Make a txt table from ccit or vice versa.\n\n");
  fprintf(stderr, "Positional arguments:\n");
  fprintf(stderr, "  file               File for convert.\n\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -h, --help         Show this help message and exit.\n");
  fprintf(stderr, "  -c, --ccit         Convert txt table to ccit.\n");
  fprintf(stderr, "  -p, --path <PATH>  Path to out file.\n");
}

int main(int argc, char **argv) {
  // Parse arguments
  bool to_ccit = false;
  std::filesystem::path input_file, output_path;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--ccit") == 0) {
      to_ccit = true;
    } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--path") == 0) {
      if (i + 1 < argc) {
        output_path = argv[++i];
      } else {
        usage(argv[0]);
        fprintf(stderr, "Error: --path requires an argument.\n");
        return -1;
      }
    } else if (input_file.empty()) {
      input_file = argv[i];
    } else {
      usage(argv[0]);
      fprintf(stderr, "Error: Unknown argument or multiple input files: %s\n",
                      argv[i]);
      return -1;
    }
  }

  if (input_file.empty()) {
    fprintf(stderr, "Error: No input file specified.\n");
    usage(argv[0]);
    return -1;
  }

  // Determine output path
  if (output_path.empty()) {
    output_path = input_file.parent_path() / (input_file.stem().string() +
                  (to_ccit ? ".ccit" : ".txt"));
  }

  fprintf(stderr, "Converting %s to %s\n", input_file.string().c_str(),
                  output_path.string().c_str());

  // Read input file
  std::string input_data;
  if (!mg::fs::read_file(input_file.string().c_str(), input_data)) {
    return -1;
  }

  // Convert data
  std::string output_data;
  if (to_ccit) {
    if (!mg::data::txt_to_ccit(input_data, output_data)) {
      fprintf(stderr, "Fail to convert txt to ccit.\n");
      return -1;
    }
  } else {
    if (!mg::data::ccit_to_txt(input_data, output_data)) {
      fprintf(stderr, "Fail to convert ccit to txt.\n");
      return -1;
    }
  }

  std::filesystem::path output_dir = output_path.parent_path();

  if (!output_dir.empty() && !std::filesystem::is_directory(output_dir)) {
    if (!std::filesystem::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory: %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  // Write output file
  if (!mg::fs::write_file(output_path.string().c_str(), output_data)) {
    return -1;
  }

  return 0;
}