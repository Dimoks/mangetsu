#include <stdio.h>
#include <algorithm>
#include <functional>
#include <map>

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>
#include <imgui_memory_editor.h>

#include <mg/data/mzp.hpp>
#include <mg/data/mzx.hpp>
#include <mg/data/nam.hpp>
#include <mg/util/endian.hpp>
#include <mg/util/fs.hpp>
#include <mg/util/string.hpp>
// per imgui docs, unifont converted to cpp + included here for embedding
#include <unifont.h>

enum DataType {
  UNDEFINED,
  DATA_NAM,
  DATA_MZP,
  DATA_MZX,
  DATA_HEXDUMP,
  DATA_STRING_TABLE,
};

struct DataFile;
std::vector<std::shared_ptr<DataFile>> data_file_contexts;

struct DataFile {
  std::string file_name;
  std::string raw_data;
  DataType display_type = UNDEFINED;
  bool is_open = true;
  bool is_root = false;

  DataFile *parent;
  unsigned parent_string_table_idx = -1;

  bool parsed_data_valid;
  void *parsed_data;

  MemoryEditor mem_edit;

  bool render() {
    // Data type selectable
    if (!is_open && !is_root) return false;

    ImGui::Begin(file_name.c_str(), is_root ? nullptr : &is_open);
    if (!is_open) {
      ImGui::End();
      return false;
    }

    ImGui::Text("Interpret data as:");
    if (ImGui::Selectable("Hexdump", display_type == DATA_HEXDUMP)) {
      display_type = DATA_HEXDUMP;
      mem_edit.ReadOnly = true;
      parsed_data_valid = true;
    }
    if (ImGui::Selectable("NAM", display_type == DATA_NAM)) {
      display_type = DATA_NAM;
      parsed_data = new mg::data::Nam;
      parsed_data_valid = mg::data::nam_read(
          raw_data, *reinterpret_cast<mg::data::Nam *>(parsed_data));
    }
    if (ImGui::Selectable("MZP", display_type == DATA_MZP)) {
      display_type = DATA_MZP;
      parsed_data = new mg::data::Mzp;
      parsed_data_valid = mg::data::mzp_read(
          raw_data, *reinterpret_cast<mg::data::Mzp *>(parsed_data));
    }
    if (ImGui::Selectable("MZX", display_type == DATA_MZX)) {
      display_type = DATA_MZX;
      parsed_data = new std::string;
      parsed_data_valid = mg::data::mzx_decompress(
          raw_data, *reinterpret_cast<std::string *>(parsed_data));
    }
    if (ImGui::Selectable("String Table", display_type == DATA_STRING_TABLE)) {
      display_type = DATA_STRING_TABLE;
      parsed_data = new std::vector<std::string>;
      parsed_data_valid = true;
    }

    // Delegate detail render
    if (!parsed_data_valid) {
      ImGui::Text("Cannot interpret data this way.");
      ImGui::End();
      return false;
    }

    bool result = false;
    switch (display_type) {
    case DATA_NAM:
      result = render_nam();
      break;
    case DATA_MZP:
      result = render_mzp();
      break;
    case DATA_MZX:
      result = render_mzx();
      break;
    case DATA_HEXDUMP:
      result = render_hex();
      break;
    case DATA_STRING_TABLE:
      result = render_string_table();
      break;
    default:
      break;
    }

    ImGui::End();
    return result;
  }

  bool render_string_table() {
    if (parent == nullptr) {
      ImGui::Text("No parent to retrieve string table from");
      return false;
    }

    if (parent->display_type != DATA_MZP) {
      ImGui::Text("Parent is not of type MZP");
      return false;
    }

    // Structure to hold string data with cached wrapped text and selection
    struct StringEntry {
      std::string text = "";         // Original string content
      std::string wrapped_text = ""; // Cached text with word wrapping
      float wrap_width = 0.0f;       // Width used for wrapping
      int wrapped_lines_count = 1;   // Number of wrapped lines
      int selection_start = 0;       // Start of text selection (in bytes)
      int selection_end = 0;         // End of text selection (in bytes)
      bool activate_cursor = false;  // Flag to activate cursor in next frame
      int cursor_pos = 0;            // Desired cursor position
      bool has_selection() const { return selection_start != selection_end; }
    };

    // Structure to store input bounds for visible entries
    struct InputBounds {
      int idx;
      ImVec2 pos;
      ImVec2 size;
      const std::string *text; // Pointer to wrapped_text for cursor calculation
    };

    // Structure to hold per-table state
    struct RenderState {
      bool was_popup_open = false;
      int target_entry_idx = -1;
      int target_cursor_pos = 0;
      ImVec2 mouse_pos = ImVec2(0, 0);
      std::vector<InputBounds> input_bounds;
    };

    // Use static map to store state per DataFile instance
    static std::map<void*, RenderState> table_states;
    RenderState& state = table_states[this];

    // Initialize parsed_data if not already done
    if (!parsed_data) {
      parsed_data = new std::vector<StringEntry>;
    }
    std::vector<StringEntry> *strings =
        reinterpret_cast<std::vector<StringEntry> *>(parsed_data);

    // Generate string table options
    mg::data::Mzp *parent_mzp =
        reinterpret_cast<mg::data::Mzp *>(parent->parsed_data);
    for (unsigned i = 0; i < parent_mzp->entry_headers.size(); i++) {
      if (ImGui::Selectable(mg::string::format("Entry %u", i).c_str(),
                            parent_string_table_idx == i)) {
        parent_string_table_idx = i;

        // Re-extract string table indices
        // Note string tables are big endian
        strings->clear();

        uint32_t *offsets =
            reinterpret_cast<uint32_t *>(parent_mzp->entry_data[i].data());
        const unsigned offset_count =
            parent_mzp->entry_data[i].size() / sizeof(uint32_t);
        strings->reserve(offset_count); // Preallocate memory
        for (unsigned j = 0; j < offset_count; ++j) {
          uint32_t start_offset = mg::be_to_host_u32(offsets[j]);
          uint32_t end_offset = j < offset_count - 1
                                    ? mg::be_to_host_u32(offsets[j + 1])
                                    : raw_data.size();
          if (start_offset >= raw_data.size() || end_offset > raw_data.size()) {
            strings->emplace_back(StringEntry{mg::string::format(
                "Invalid offset range %08x - %08x", start_offset, end_offset)});
            continue;
          }
          strings->emplace_back(StringEntry{std::string(&raw_data[start_offset],
                                            end_offset - start_offset)});
        }
      }
    }

    // Return if no strings to display
    if (strings->empty()) return false;

    // Helper function to find next UTF-8 character boundary
    auto next_utf8_char = [](const char *p, const char *end) -> const char * {
      if (p >= end || *p == 0) return p;
      p++;
      while (p < end && (*p & 0xC0) == 0x80) p++; // Skip continuation bytes
      return p;
    };

    // Apply styling for the table
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));

    // Setup clipper for efficient rendering of visible rows only
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(strings->size()));
    const float font_height = ImGui::GetFontSize();
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
    ImFont *font = ImGui::GetFont();

    // Track popup state for this frame
    bool is_popup_open = false;

    // Detect popup closure and capture mouse position
    if (state.was_popup_open &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
      state.mouse_pos = ImGui::GetMousePos();
      state.target_entry_idx = -1;
      // Find input field under mouse
      for (const auto &bounds : state.input_bounds) {
        if (state.mouse_pos.x >= bounds.pos.x &&
            state.mouse_pos.x < bounds.pos.x + bounds.size.x &&
            state.mouse_pos.y >= bounds.pos.y &&
            state.mouse_pos.y < bounds.pos.y + bounds.size.y) {
          state.target_entry_idx = bounds.idx;
          // Calculate cursor position based on mouse with UTF-8 awareness
          float rel_x = state.mouse_pos.x - (bounds.pos.x + frame_padding.x);
          float rel_y = state.mouse_pos.y - (bounds.pos.y + frame_padding.y);
          int line_no = static_cast<int>(rel_y / font_height);
          const char *text_start = bounds.text->c_str();
          const char *text_end = text_start + bounds.text->size();
          const char *line_start = text_start;
          for (int l = 0; l < line_no && *line_start; ++l) {
            while (*line_start && *line_start != '\n') {
              line_start = next_utf8_char(line_start, text_end);
            }
            if (*line_start == '\n') line_start++;
          }
          const char *line_end = line_start;
          while (*line_end && *line_end != '\n') {
            line_end = next_utf8_char(line_end, text_end);
          }
          int cursor_pos = line_start - text_start;
          float current_x = 0.0f;
          const char *p = line_start;
          while (p < line_end) {
            const char *next_p = next_utf8_char(p, text_end);
            float char_width = font->CalcTextSizeA(font_height, FLT_MAX,
                                                    0.0f, p, next_p).x;
            if (rel_x <= current_x + char_width) {
              // Determine if click is closer to left or right edge
              float mid_point = current_x + char_width / 2.0f;
              cursor_pos = (rel_x < mid_point) ? (p - text_start) :
                                                (next_p - text_start);
              break;
            }
            current_x += char_width;
            cursor_pos = next_p - text_start;
            p = next_p;
          }
          if (p >= line_end) {
            cursor_pos = line_end - text_start;
          }
          state.target_cursor_pos = cursor_pos;
          break; // Stop at first matching field
        }
      }
    }

    // Clear input bounds for current frame
    state.input_bounds.clear();

    // Lambda to process selection line by line
    auto process_selection = [&](const StringEntry &entry,
                                  bool copy_to_clipboard,
                                  std::function<void(const char*, const char*,
                                      const char*, float)> callback) {
      int sel_start = entry.selection_start;
      int sel_end = entry.selection_end;
      if (sel_start > sel_end) std::swap(sel_start, sel_end);

      const char *text_begin = entry.wrapped_text.c_str();
      const char *text_end = text_begin + entry.wrapped_text.size();
      const char *text_selected_begin = text_begin + sel_start;
      const char *text_selected_end = std::min(text_end, text_begin + sel_end);

      std::string selected_text;
      if (copy_to_clipboard) {
        selected_text.reserve(sel_end - sel_start);
      }

      float y_offset = 0.0f;
      const char *p = text_begin;

      while (p < text_end && (p < text_selected_end || callback == nullptr)) {
        const char *line_start = p;
        const char *line_end = p;
        while (*line_end && *line_end != '\n' && line_end < text_end) {
          line_end++;
        }
        if (line_end < text_end && *line_end == '\n') {
          line_end++;
        }

        if (p < text_selected_end) {
          const char *sel_start_in_line = std::max(p, text_selected_begin);
          const char *sel_end_in_line = std::min(line_end, text_selected_end);
          if (sel_start_in_line < sel_end_in_line) {
            if (callback) {
              callback(line_start, sel_start_in_line,
                        sel_end_in_line, y_offset);
            }
            if (copy_to_clipboard) {
              // Map positions to original text
              size_t newline_count_start = 0;
              size_t newline_count_end = 0;
              for (const char *q = text_begin; q < sel_end_in_line; ++q) {
                if (*q == '\n') {
                  if (q < sel_start_in_line) newline_count_start++;
                  newline_count_end++;
                }
              }
              size_t wrapped_start = sel_start_in_line - text_begin;
              size_t wrapped_end = sel_end_in_line - text_begin;
              size_t start_idx = wrapped_start - newline_count_start;
              size_t end_idx = wrapped_end - newline_count_end;
              if (end_idx > entry.text.length()) {
                end_idx = entry.text.length();
              }
              if (start_idx < end_idx) {
                selected_text.append(entry.text, start_idx,
                                      end_idx - start_idx);
              }
            }
          }
        }

        p = line_end;
        y_offset += font_height;
      }

      if (copy_to_clipboard && !selected_text.empty()) {
        ImGui::SetClipboardText(selected_text.c_str());
      }
    };

    while (clipper.Step()) {
      const float current_wrap_width = ImGui::GetContentRegionAvail().x;

      for (int idx = clipper.DisplayStart; idx < clipper.DisplayEnd; idx++) {
        const size_t i = static_cast<size_t>(idx);
        ImGui::PushID(idx);

        // Render separator and index
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%d", idx);

        StringEntry &entry = (*strings)[i];

        // Update wrapped text only if wrap width
        // changed or wrapped text is empty
        if (entry.wrap_width != current_wrap_width ||
            entry.wrapped_text.empty()) {
          entry.wrapped_text.clear();
          entry.wrapped_lines_count = 1;
          const char *text_start = entry.text.c_str();
          const char *text_end = text_start + entry.text.size();
          while (text_start < text_end) {
            const char *line_end = font->CalcWordWrapPositionA(
                1.0f, text_start, text_end, current_wrap_width);
            if (line_end == text_start) line_end++;
            entry.wrapped_text.append(text_start, line_end);
            if (line_end < text_end && *line_end != '\n') {
              entry.wrapped_text += '\n';
              entry.wrapped_lines_count++;
            }
            text_start = line_end;
          }
          entry.wrap_width = current_wrap_width;
        }

        const float height = (entry.wrapped_lines_count + 1) * line_height;

        // Apply transparent background
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

        // Store input position and size
        ImVec2 input_pos = ImGui::GetCursorScreenPos();
        ImVec2 input_size = ImVec2(current_wrap_width, height);

        // Save bounds for next frame
        state.input_bounds.push_back({idx, input_pos, input_size,
                                      &entry.wrapped_text});

        // Input callback to handle selection and cursor positioning
        auto input_callback = [](ImGuiInputTextCallbackData *data) -> int {
          StringEntry *entry = static_cast<StringEntry *>(data->UserData);
          if (data->HasSelection()) {
            entry->selection_start = data->SelectionStart;
            entry->selection_end = data->SelectionEnd;
          } else {
            entry->selection_start = entry->selection_end = 0;
          }
          // Set cursor position if activation is requested
          if (entry->activate_cursor) {
            data->CursorPos = entry->cursor_pos;
            data->ClearSelection();
            entry->activate_cursor = false;
          }
          return 0;
        };

        // Render input text
        if (entry.activate_cursor) {
          // Check if window is focused, attempt to focus if not
          if (!ImGui::IsWindowFocused()) {
            ImGui::SetWindowFocus();
          }
          ImGui::SetKeyboardFocusHere();
        }

        ImGui::InputTextMultiline(("##input_" + std::to_string(idx)).c_str(),
                                  const_cast<char*>(entry.wrapped_text.c_str()),
                                  entry.wrapped_text.size() + 1, input_size,
                                  ImGuiInputTextFlags_ReadOnly |
                                  ImGuiInputTextFlags_NoHorizontalScroll |
                                  ImGuiInputTextFlags_CallbackAlways,
                                  input_callback, &entry);

        ImGui::PopStyleColor();

        // Render custom selection for inactive input
        if (!ImGui::IsItemActive() && entry.has_selection()) {
          ImDrawList *draw_list = ImGui::GetWindowDrawList();
          ImU32 bg_color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

          process_selection(entry, false, [&](const char *line_start,
                                              const char *sel_start_in_line,
                                              const char *sel_end_in_line,
                                              float y_offset) {
            float x_start = input_pos.x + frame_padding.x +
                            font->CalcTextSizeA(font_height, FLT_MAX, 0.0f,
                                                line_start,
                                                sel_start_in_line).x;
            float x_end = input_pos.x + frame_padding.x +
                          font->CalcTextSizeA(font_height, FLT_MAX, 0.0f,
                                              line_start, sel_end_in_line).x;

            ImVec2 rect_min(x_start, input_pos.y + frame_padding.y + y_offset);
            ImVec2 rect_max(x_end, input_pos.y + frame_padding.y + y_offset +
                            font_height);
            draw_list->AddRectFilled(rect_min, rect_max, bg_color);
          });
        }

        // Handle Ctrl+C for copying selected text
        if (ImGui::IsItemActive() && entry.has_selection() &&
            ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl) {
          process_selection(entry, true, nullptr);
        }

        // Handle context menu
        if (ImGui::BeginPopupContextItem()) {
          if (entry.has_selection() && ImGui::MenuItem("Copy Selected")) {
            process_selection(entry, true, nullptr);
          } else if (ImGui::MenuItem("Copy Entire Row")) {
            ImGui::SetClipboardText(entry.text.c_str());
          } else {
            is_popup_open = true;
          }
          ImGui::EndPopup();
        }

        // Activate target entry after popup closes
        if (state.was_popup_open && !is_popup_open &&
            state.target_entry_idx == idx) {
          entry.activate_cursor = true;
          entry.cursor_pos = state.target_cursor_pos;
          state.target_entry_idx = -1; // Reset after activation
        }

        ImGui::PopID();
      }
    }

    // Reset popup tracking
    state.was_popup_open = is_popup_open;

    ImGui::PopStyleVar(2);
    return false;
  }

  bool render_hex() {
    mem_edit.DrawContents(raw_data.data(), raw_data.size());
    return false;
  }

  bool render_nam() {
    mg::data::Nam *nam = reinterpret_cast<mg::data::Nam *>(parsed_data);

    ImGuiListClipper clipper;
    clipper.Begin(nam->names.size());

    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        auto &name = nam->names[i];

        ImGui::Separator();
        ImGui::Text("%d", i);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::InputText(("##name_" + std::to_string(i)).c_str(),
                            const_cast<char*>(name.c_str()),
                            name.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
      }
    }
    return false;
  }

  bool render_mzp() {
    mg::data::Mzp *mzp = reinterpret_cast<mg::data::Mzp *>(parsed_data);
    ImGui::Text("MZP with %zu entries", mzp->entry_headers.size());

    bool did_add_ctx = false;
    for (unsigned i = 0; i < mzp->entry_headers.size(); i++) {
      auto &entry = mzp->entry_headers[i];
      ImGui::PushID(i);
      ImGui::Text("Entry %4u of size %08x offset %08x", i,
                  entry.entry_data_size(),
                  mzp->archive_entry_start_offset(entry));
      ImGui::SameLine();
      if (ImGui::Button("Open Subarchive")) {
        // Create a new datafile context
        auto ctx = std::make_unique<DataFile>();
        ctx->file_name =
            mg::string::format("%s (MZP) @ %u", file_name.c_str(), i);
        ctx->raw_data = mzp->entry_data[i];
        ctx->parent = this;
        data_file_contexts.emplace_back(std::move(ctx));
        did_add_ctx = true;
      }
      ImGui::PopID();
    }

    return did_add_ctx;
  }

  bool render_mzx() {
    std::string *data = reinterpret_cast<std::string *>(parsed_data);
    mem_edit.ReadOnly = true;
    mem_edit.DrawContents(data->data(), data->size());

    return false;
  }
};

int main(int argc, char **argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  if (argc != 2) {
    fprintf(stderr, "%s data_file\n", argv[0]);
    return -1;
  }

  // GL backend init
  glfwSetErrorCallback([](int error, const char *description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
  });
  if (!glfwInit()) {
    return -1;
  }
  GLFWwindow *window =
      glfwCreateWindow(1024, 768, "Data Explorer", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  // Imgui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // Setup ImGui IO
  ImGuiIO &io = ImGui::GetIO();
  io.FontGlobalScale = 1.0f;

  // Render bindings
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();

  // Load fonts
  ImVector<ImWchar> ranges;
  ImFontGlyphRangesBuilder builder;
  builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
  builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
  static const ImWchar customRanges[] = {0x2600, 0x26FF, 0};
  builder.AddRanges(customRanges);
  builder.BuildRanges(&ranges);
  io.Fonts->AddFontFromMemoryCompressedTTF(Unifont_compressed_data,
    Unifont_compressed_size, 16.0f, nullptr, ranges.Data);

  // Emplace the root data file
  auto root_file = std::make_unique<DataFile>();
  root_file->file_name = argv[1];
  root_file->is_root = true;
  if (!mg::fs::read_file(argv[1], root_file->raw_data)) {
    fprintf(stderr, "Failed to load root file\n");
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }
  data_file_contexts.emplace_back(std::move(root_file));

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Display a window for each file
    auto it = data_file_contexts.begin();
    while (it != data_file_contexts.end()) {
      if ((*it)->render()) {
        it = data_file_contexts.begin();
        continue;
      }
      ++it;
    }

    // Rendering
    data_file_contexts.erase(
      std::remove_if(data_file_contexts.begin(), data_file_contexts.end(),
        [](const std::shared_ptr<DataFile> &ctx) {
          return !ctx->is_open && !ctx->is_root;
        }),
      data_file_contexts.end());

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glfwMakeContextCurrent(window);
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
