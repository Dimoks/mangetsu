#include <cstring>
#include <filesystem>
#include <string>
#include <mg/data/cbg.hpp>
#include <mg/util/fs.hpp>

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

  if ((argc - arg_offset) != 2 && (argc - arg_offset) != 3) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  CBG to PNG: %s input_cbg output_png\n", argv[0]);
    fprintf(stderr, "  PNG to CBG: %s [--verify|-v] input_cbg input_png "
                    "output_cbg\n", argv[0]);
    fprintf(stderr, "  --verify|-v convert output_cbg to PNG "
                    "for verification.\n");
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
    std::filesystem::path output_path(argv[arg_offset + 1]);
    std::string output_ext = output_path.extension().string();
    for (char &c : output_ext) c = tolower(c); // Case-insensitive extension
    if (output_ext != ".png") {
      fprintf(stderr, "Output file must have .png extension "
                      "for CBG->PNG conversion.\n");
      return -1;
    }

    // Ensure output directory exists
    std::filesystem::path output_dir = output_path.parent_path();
    if (!output_dir.empty() && !std::filesystem::is_directory(output_dir)) {
      if (!std::filesystem::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create output directory: %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }

    // Log conversion direction
    fprintf(stderr, "Converting CBG to PNG: %s -> %s\n", argv[arg_offset],
                    argv[arg_offset + 1]);

    // Convert CBG to PNG
    std::string png_output;
    if (!cbg.img_write(png_output)) {
      fprintf(stderr, "Failed to convert CBG to PNG.\n");
      return -1;
    }

    // Write output PNG
    if (!mg::fs::write_file(argv[arg_offset + 1], png_output)) {
      return -1;
    }
    fprintf(stderr, "Wrote %s\n", argv[arg_offset + 1]);
  } else {
    // PNG -> CBG conversion
    std::filesystem::path output_path(argv[arg_offset + 2]);
    std::string output_ext = output_path.extension().string();
    for (char &c : output_ext) c = tolower(c); // Case-insensitive extension
    if (output_ext != ".cbg") {
      fprintf(stderr, "Output file must have .cbg extension "
                      "for PNG->CBG conversion.\n");
      return -1;
    }

    // Ensure output directory exists
    std::filesystem::path output_dir = output_path.parent_path();
    if (!output_dir.empty() && !std::filesystem::is_directory(output_dir)) {
      if (!std::filesystem::create_directories(output_dir)) {
        fprintf(stderr, "Failed to create output directory: %s\n",
                        output_dir.string().c_str());
        return -1;
      }
    }

    // Read PNG file
    std::string png_data;
    if (!mg::fs::read_file(argv[arg_offset + 1], png_data)) {
      return -1;
    }

    // Log conversion direction
    fprintf(stderr, "Converting PNG to CBG: %s -> %s\n",
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

    // Write to file
    if (!mg::fs::write_file(argv[arg_offset + 2], output_data)) {
      return -1;
    }
    fprintf(stderr, "Wrote %s\n", argv[arg_offset + 2]);

    // Convert back to PNG for verification if requested
    if (verify) {
      std::string png_output;
      if (!cbg.img_write(png_output)) {
        fprintf(stderr, "Failed to convert CBG to PNG for verification.\n");
        return -1;
      }

      std::string png_path =
          std::filesystem::path(output_path).replace_filename(
               output_path.stem().string() + "_verify.png").string();

      // Write verification PNG
      if (!mg::fs::write_file(png_path.c_str(), png_output)) {
        return -1;
      }
      fprintf(stderr, "Wrote verification PNG: %s\n", png_path.c_str());
    }
  }

  return 0;
}
