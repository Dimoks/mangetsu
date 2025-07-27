#include <filesystem>
#include <mg/data/lenzu.hpp>
#include <mg/util/fs.hpp>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "%s infile outfile [invert]\n", argv[0]);
    fprintf(stderr, "  invert: 0 (false) or 1 (true).\n");
    return -1;
  }

  // Parse invert option
  bool invert = true;
  if (argc >= 4) {
    int invert_value = std::stoi(argv[3]);
    if (invert_value == 0) {
      invert = false;
    } else if (invert_value != 1) {
      fprintf(stderr, "Invalid invert value: use 0 (false) or 1 (true).\n");
      return -1;
    }
  }

  // Read input file
  std::string compressed;
  if (!mg::fs::read_file(argv[1], compressed)) {
    return -1;
  }

  // Decompress
  std::string decompressed;
  if (!mg::data::Lenzu::decompress(compressed, decompressed, invert)) {
    fprintf(stderr, "Decompression failed.\n");
    return -1;
  }

  // Prepare output path
  std::filesystem::path output_path(argv[2]);
  std::filesystem::path output_dir = output_path.parent_path();

  if (!output_dir.empty() && !std::filesystem::is_directory(output_dir)) {
    try {
      std::filesystem::create_directories(output_dir);
    } catch (const std::filesystem::filesystem_error &e) {
      fprintf(stderr, "Failed to create directory: %s\n", e.what());
      return -1;
    }
  }

  // Write output
  if (!mg::fs::write_file(argv[2], decompressed)) {
    return -1;
  }

  return 0;
}
