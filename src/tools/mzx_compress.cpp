#include <filesystem>
#include <mg/data/mzx.hpp>
#include <mg/util/fs.hpp>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 5) {
    fprintf(stderr, "%s infile outfile [compression_level] [invert]\n", argv[0]);
    fprintf(stderr, "  compression_level: 0, 1, or 2 (default: 0)\n");
    fprintf(stderr, "    0: No compression (fastest)\n");
    fprintf(stderr, "    1: Basic compression (RLE)\n");
    fprintf(stderr, "    2: Advanced compression (RLE + BackRef + RingBuf)\n");
    fprintf(stderr, "  invert: 0 (false) or 1 (true)\n");
    return -1;
  }

  int compression_level = 0;
  if (argc >= 4) {
    compression_level = std::stoi(argv[3]);
    if (compression_level < 0 || compression_level > 2) {
      fprintf(stderr, "Invalid compression level: use 0, 1, or 2\n");
      return -1;
    }
  }

  bool invert = false;
  if (argc >= 5) {
    int invert_value = std::stoi(argv[4]);
    if (invert_value == 1) {
      invert = true;
    } else if (invert_value != 0) {
      fprintf(stderr, "Invalid invert value: use 0 (false) or 1 (true)\n");
      return -1;
    }
  }

  // Read input file
  std::string raw;
  if (!mg::fs::read_file(argv[1], raw)) {
    return -1;
  }

  // Compress
  std::string compressed;
  if (!mg::data::mzx_compress(raw, compressed, compression_level, invert)) {
    fprintf(stderr, "Compress failed\n");
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
  if (!mg::fs::write_file(argv[2], compressed)) {
    return -1;
  }

  return 0;
}
