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
  std::filesystem::path output_dir =
       std::filesystem::path(argv[2]).parent_path();

  if (!output_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
      fprintf(stderr, "Failed to create output directory: %s: %s\n",
                      output_dir.string().c_str(), ec.message().c_str());
      return -1;
    }
  }

  // Write output
  if (!mg::fs::write_file(argv[2], decompressed)) {
    return -1;
  }

  return 0;
}
