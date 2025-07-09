#include <filesystem>
#include <mg/data/mzpimage.hpp>
#include <mg/util/fs.hpp>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s input.mzp output.png [overlap]\n", argv[0]);
    fprintf(stderr, "  overlap: 0 (false) or 1 (true)\n");
    return -1;
  }

  bool overlap = false;

  if (argc >= 4) {
    int overlap_value = std::stoi(argv[3]);
    if (overlap_value == 1) {
      overlap = true;
    } else if (overlap_value != 0) {
      fprintf(stderr, "Invalid overlap value: use 0 (false) or 1 (true)\n");
      return -1;
    }
  }

  std::string mzp_data;
  if (!mg::fs::read_file(argv[1], mzp_data)) {
    return -1;
  }

  mg::data::MzpImage image(mzp_data);
  if (!image.is_valid()) {
    fprintf(stderr, "Failed to parse MZP image.\n");
    return -1;
  }

  std::string png_data;
  if (!image.img_write(png_data, overlap)) {
    fprintf(stderr, "Failed to convert image to PNG.\n");
    return -1;
  }

  if (png_data.empty()) {
    fprintf(stderr, "PNG data is empty!\n");
    return -1;
  }

  std::filesystem::path output_path(argv[2]);
  std::filesystem::path output_dir = output_path.parent_path();

  if (!output_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
      fprintf(stderr, "Failed to create output directory: %s: %s\n",
                      output_dir.string().c_str(), ec.message().c_str());
      return -1;
    }
  }

  if (!mg::fs::write_file(output_path.string().c_str(), png_data)) {
    return -1;
  }

  printf("Image written to: %s\n", output_path.string().c_str());
  return 0;
}
