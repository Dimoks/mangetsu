#include <filesystem>
#include <string>
#include <mg/data/mzpimage.hpp>
#include <mg/util/fs.hpp>

int main(int argc, char **argv) {
  int compression_level = 0;
  std::string input_path;
  std::string output_path;
  std::string template_mzp_path;
  bool overlap = false;
  int non_option_args = 0;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "-c" || arg == "--compression-level") {
      if (i + 1 < argc) {
        compression_level = atoi(argv[++i]);
        if (compression_level < 0 || compression_level > 2) {
          fprintf(stderr, "Invalid compression level: %d. "
                          "Valid levels are 0, 1, or 2.\n", compression_level);
          return -1;
        }
      } else {
        fprintf(stderr, "Option -c/--compression-level "
                        "requires an argument.\n");
        return -1;
      }
    } else if (arg == "-t" || arg == "--template") {
      if (i + 1 < argc) {
        template_mzp_path = argv[++i];
      } else {
        fprintf(stderr, "Option -t/--template requires an argument.\n");
        return -1;
      }
    } else if (arg == "-o" || arg == "--overlap") {
      overlap = true;
  } else {
      if (non_option_args == 0) {
        input_path = arg;
        non_option_args++;
      } else if (non_option_args == 1) {
        output_path = arg;
        non_option_args++;
      } else {
        fprintf(stderr, "Too many arguments: %s\n", arg.c_str());
        return -1;
      }
    }
  }

  if (input_path.empty()) {
    fprintf(stderr, "Usage: %s [options] input.png [output.mzp]\n", argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --compression-level <0|1|2>  "
                    "Set compression level:\n"
                    "                                     "
                    "0 (no compression),\n"
                    "                                     "
                    "1 (fast), 2 (best).\n");
    fprintf(stderr, "  -t, --template <template.mzp>    "
                    "Set template mzp.\n");
    fprintf(stderr, "  -o, --overlap                    "
                    "Overlap tiles when slicing.\n");
    return -1;
  }

  if (output_path.empty()) {
    std::filesystem::path input_file_path(input_path);
    output_path = input_file_path.replace_extension(".mzp").string();
  }

  if (template_mzp_path.empty()) {
    std::filesystem::path input_file_path(input_path);
    template_mzp_path = input_file_path.replace_extension(".mzp").string();
  }

  std::string png_data;
  if (!mg::fs::read_file(input_path.c_str(), png_data)) {
    return -1;
  }

  std::string mzp_data;
  if (std::filesystem::is_regular_file(template_mzp_path)) {
    if (!mg::fs::read_file(template_mzp_path.c_str(), mzp_data)) {
      return -1;
    }
  } else {
    fprintf(stderr, "Template MZP file not found: %s\n",
                    template_mzp_path.c_str());
    return -1;
  }

  mg::data::MzpImage mzpImg(mzp_data);
  if (!mzpImg.is_valid()) {
    fprintf(stderr, "Failed to parse template MZP file: %s\n",
                    template_mzp_path.c_str());
    return -1;
  }

  if (!mzpImg.img_read(png_data, compression_level, overlap)) {
    fprintf(stderr, "Failed to update MZP image with PNG data.\n");
    return -1;
  }

  std::string mzp_output_data;
  if (!mzpImg.write_mzp(mzp_output_data)) {
    fprintf(stderr, "Failed to write MZP archive.\n");
    return -1;
  }

  std::filesystem::path output_dir =
      std::filesystem::path(output_path).parent_path();
  if (!output_dir.empty() && !std::filesystem::is_directory(output_dir)) {
    try {
      std::filesystem::create_directories(output_dir);
    } catch (const std::filesystem::filesystem_error &e) {
      fprintf(stderr, "Failed to create directory: %s\n", e.what());
      return -1;
    }
  }

  if (!mg::fs::write_file(output_path.c_str(), mzp_output_data)) {
    return -1;
  }

  fprintf(stderr, "MzpImage written to %s\n", output_path.c_str());
  return 0;
}
