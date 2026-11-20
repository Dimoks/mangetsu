#include <string>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <mg/data/cbg.hpp>
#include <mg/util/fs.hpp>
#include <mg/util/string.hpp>

namespace fs = std::filesystem;
using mg::string::toLower;

static void usage(const char* prog) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  CBG to PNG: %s input_cbg output_png\n", prog);
  fprintf(stderr, "  PNG to CBG: %s [--verify|-v] input_cbg input_png "
                  "output_cbg\n", prog);
  fprintf(stderr, "  --verify|-v convert output_cbg to PNG "
                  "for verification.\n");
}

int main(int argc, char** argv) {
  // Check arguments
  bool verify = false;
  int arg_offset = 1;

  // Parse --verify or -v option
  if (argc > 1 && (std::strcmp(argv[1], "--verify") == 0 ||
      std::strcmp(argv[1], "-v") == 0)) {
    verify = true;
    arg_offset++;
  }

  if (argc > arg_offset && argv[arg_offset][0] == '-') {
    fprintf(stderr, "Unknown option: %s\n", argv[arg_offset]);
    usage(argv[0]);
    return -1;
  }

  if (argc != arg_offset + 2 && argc != arg_offset + 3) {
    usage(argv[0]);
    return -1;
  }

  // Read CBG file
  std::string cbg_data;
  if (!mg::fs::read_file(argv[arg_offset], cbg_data)) {
    return -1;
  }

  // Initialize CompressedBG
  mg::data::CompressedBG cbg(cbg_data);
  if (!cbg.is_valid()) {
    fprintf(stderr, "Failed to initialize CBG object from %s\n",
                    argv[arg_offset]);
    return -1;
  }

  // Determine conversion direction based on number of arguments
  if ((argc - arg_offset) == 2) {
    // CBG -> PNG conversion
    fs::path output_path(argv[arg_offset + 1]);
    std::string output_ext = toLower(output_path.extension().string());

    if (output_ext != ".png") {
      fprintf(stderr, "Output file must have .png extension "
                      "for CBG->PNG conversion.\n");
      return -1;
    }

    // Log conversion direction
    printf("Converting CBG to PNG: %s -> %s\n", argv[arg_offset],
            argv[arg_offset + 1]);

    // Convert CBG to PNG
    std::string png_output;
    if (!cbg.img_write(png_output)) {
      fprintf(stderr, "Failed to convert CBG to PNG.\n");
      return -1;
    }

    // Ensure output directory exists
    fs::path output_dir = output_path.parent_path();
    if (!output_dir.empty()) {
      std::error_code ec;
      fs::create_directories(output_dir, ec);
      if (ec) {
        fprintf(stderr, "Failed to create output directory: %s: %s\n",
                        output_dir.string().c_str(), ec.message().c_str());
        return -1;
      }
    }

    // Write output PNG
    if (!mg::fs::write_file(argv[arg_offset + 1], png_output)) {
      return -1;
    }
    printf("Wrote %s\n", argv[arg_offset + 1]);
  } else {
    // PNG -> CBG conversion
    fs::path output_path(argv[arg_offset + 2]);
    std::string output_ext = toLower(output_path.extension().string());

    if (output_ext != ".cbg") {
      fprintf(stderr, "Output file must have .cbg extension "
                      "for PNG->CBG conversion.\n");
      return -1;
    }

    // Read PNG file
    std::string png_data;
    if (!mg::fs::read_file(argv[arg_offset + 1], png_data)) {
      return -1;
    }

    // Log conversion direction
    printf("Converting PNG to CBG: %s -> %s\n",
            argv[arg_offset + 1], argv[arg_offset + 2]);

    // Convert PNG to CBG
    if (!cbg.img_read(png_data)) {
      fprintf(stderr, "Failed to convert PNG to CBG.\n");
      return -1;
    }

    // Write output CBG
    std::string output_data;
    if (!cbg.cbg_write(output_data)) {
      fprintf(stderr, "Failed to write CBG data.\n");
      return -1;
    }

    // Ensure output directory exists
    fs::path output_dir = output_path.parent_path();
    if (!output_dir.empty()) {
      std::error_code ec;
      fs::create_directories(output_dir, ec);
      if (ec) {
        fprintf(stderr, "Failed to create output directory: %s: %s\n",
                        output_dir.string().c_str(), ec.message().c_str());
        return -1;
      }
    }

    // Write to file
    if (!mg::fs::write_file(argv[arg_offset + 2], output_data)) {
      return -1;
    }
    printf("Wrote %s\n", argv[arg_offset + 2]);

    // Convert back to PNG for verification if requested
    if (verify) {
      std::string png_output;
      if (!cbg.img_write(png_output)) {
        fprintf(stderr, "Failed to convert CBG to PNG for verification.\n");
        return -1;
      }

      output_path.replace_filename(
             output_path.stem().string() + "_verify.png");

      // Write verification PNG
      if (!mg::fs::write_file(output_path.string().c_str(), png_output)) {
        return -1;
      }
      printf("Wrote verification PNG: %s\n", output_path.string().c_str());
    }
  }

  return 0;
}
