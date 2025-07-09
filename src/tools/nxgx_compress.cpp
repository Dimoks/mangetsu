#include <mg/data/nxx.hpp>
#include <mg/util/fs.hpp>
#include <filesystem>

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "%s input output\n", argv[0]);
    return -1;
  }

  // Name args
  const char *input_file = argv[1];
  const char *output_file = argv[2];

  // Read raw input data
  std::string raw;
  if (!mg::fs::read_file(input_file, raw)) {
    return -1;
  }

  // Compress
  std::string compressed;
  if (!mg::data::nxgx_compress(raw, compressed)) {
    fprintf(stderr, "Failed to compress\n");
    return -1;
  }

  std::filesystem::path output_dir =
       std::filesystem::path(output_file).parent_path();

  if (!output_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
      fprintf(stderr, "Failed to create output directory: %s: %s\n",
                      output_dir.string().c_str(), ec.message().c_str());
      return -1;
    }
  }

  // Write
  if (!mg::fs::write_file(output_file, compressed)) {
    return -1;
  }

  return 0;
}
