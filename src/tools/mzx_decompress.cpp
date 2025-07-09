#include <filesystem>
#include <mg/data/mzx.hpp>
#include <mg/util/fs.hpp>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "%s infile outfile [invert]\n", argv[0]);
    fprintf(stderr, "  invert: 0 (false) or 1 (true)\n");
    return -1;
  }

  bool invert = false;
  if (argc >= 4) {
    int invert_value = std::stoi(argv[3]);
    if (invert_value == 1) {
      invert = true;
    } else if (invert_value != 0) {
      fprintf(stderr, "Invalid invert value: use 0 (false) or 1 (true)\n");
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
  if (!mg::data::mzx_decompress(compressed, decompressed, invert)) {
    fprintf(stderr, "Decompress failed\n");
    return -1;
  }

  std::filesystem::path output_path(argv[2]);
  std::filesystem::path output_dir = output_path.parent_path();

  if (!output_dir.empty() && !std::filesystem::is_directory(output_dir)) {
    if (!std::filesystem::create_directories(output_dir)) {
      fprintf(stderr, "Failed to create directory: %s\n",
                      output_dir.string().c_str());
      return -1;
    }
  }

  // Emit
  if (!mg::fs::write_file(argv[2], decompressed)) {
    return -1;
  }

  return 0;
}
