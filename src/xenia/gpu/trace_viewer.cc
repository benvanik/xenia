/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_viewer.h"

#include <gflags/gflags.h>

#include <cinttypes>

#include "third_party/half/include/half.hpp"
#include "third_party/imgui/imgui.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/packet_disassembler.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/memory.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

DEFINE_string(target_trace_file, "", "Specifies the trace file to load.");

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

static const ImVec4 kColorError =
    ImVec4(255 / 255.0f, 0 / 255.0f, 0 / 255.0f, 255 / 255.0f);
static const ImVec4 kColorComment =
    ImVec4(42 / 255.0f, 179 / 255.0f, 0 / 255.0f, 255 / 255.0f);
static const ImVec4 kColorIgnored =
    ImVec4(100 / 255.0f, 100 / 255.0f, 100 / 255.0f, 255 / 255.0f);

TraceViewer::TraceViewer() = default;

TraceViewer::~TraceViewer() = default;

int TraceViewer::Main(const std::vector<std::wstring>& args) {
  // Grab path from the flag or unnamed argument.
  std::wstring path;
  if (!FLAGS_target_trace_file.empty()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports
    // unicode.
    path = xe::to_wstring(FLAGS_target_trace_file);
  } else if (args.size() >= 2) {
    // Passed as an unnamed argument.
    path = args[1];
  }

  // If no path passed, ask the user.
  if (path.empty()) {
    auto file_picker = xe::ui::FilePicker::Create();
    file_picker->set_mode(ui::FilePicker::Mode::kOpen);
    file_picker->set_type(ui::FilePicker::Type::kFile);
    file_picker->set_multi_selection(false);
    file_picker->set_title(L"Select Trace File");
    file_picker->set_extensions({
        {L"Supported Files", L"*.xtr"},
        {L"All Files (*.*)", L"*.*"},
    });
    if (file_picker->Show()) {
      auto selected_files = file_picker->selected_files();
      if (!selected_files.empty()) {
        path = selected_files[0];
      }
    }
  }

  if (path.empty()) {
    xe::FatalError("No trace file specified");
    return 1;
  }

  // Normalize the path and make absolute.
  auto abs_path = xe::to_absolute_path(path);

  if (!Setup()) {
    xe::FatalError("Unable to setup trace viewer");
    return 1;
  }
  if (!Load(std::move(abs_path))) {
    xe::FatalError("Unable to load trace file; not found?");
    return 1;
  }
  Run();
  return 0;
}

bool TraceViewer::Setup() {
  // Main display window.
  loop_ = ui::Loop::Create();
  window_ = xe::ui::Window::Create(loop_.get(), L"xenia-gpu-trace-viewer");
  loop_->PostSynchronous([&]() {
    xe::threading::set_name("Win32 Loop");
    if (!window_->Initialize()) {
      xe::FatalError("Failed to initialize main window");
      return;
    }
  });
  window_->on_closed.AddListener([&](xe::ui::UIEvent* e) {
    loop_->Quit();
    XELOGI("User-initiated death!");
    exit(1);
  });
  loop_->on_quit.AddListener([&](xe::ui::UIEvent* e) { window_.reset(); });
  window_->Resize(1920, 1200);

  // Create the emulator but don't initialize so we can setup the window.
  emulator_ = std::make_unique<Emulator>(L"");
  X_STATUS result =
      emulator_->Setup(window_.get(), nullptr,
                       [this]() { return CreateGraphicsSystem(); }, nullptr);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return false;
  }
  memory_ = emulator_->memory();
  graphics_system_ = emulator_->graphics_system();

  window_->set_imgui_input_enabled(true);

  window_->on_key_down.AddListener([&](xe::ui::KeyEvent* e) {
    if (e->key() == ui::KeyEvent::Key::kF5) {
      graphics_system_->ClearCaches();
      e->set_handled(true);
    }
  });

  player_ = std::make_unique<TracePlayer>(loop_.get(), graphics_system_);

  window_->on_painting.AddListener([&](xe::ui::UIEvent* e) {
    DrawUI();

    // Continuous paint.
    window_->Invalidate();
  });
  window_->Invalidate();

  return true;
}

bool TraceViewer::Load(std::wstring trace_file_path) {
  auto file_name = xe::find_name_from_path(trace_file_path);
  window_->set_title(std::wstring(L"Xenia GPU Trace Viewer: ") + file_name);

  if (!player_->Open(trace_file_path)) {
    XELOGE("Could not load trace file");
    return false;
  }

  return true;
}

void TraceViewer::Run() {
  // Wait until we are exited.
  loop_->AwaitQuit();

  player_.reset();
  emulator_.reset();
  window_.reset();
  loop_.reset();
}

void TraceViewer::DrawMultilineString(const std::string& str) {
  size_t i = 0;
  bool done = false;
  while (!done && i < str.size()) {
    size_t next_i = str.find('\n', i);
    if (next_i == std::string::npos) {
      done = true;
      next_i = str.size() - 1;
    }
    auto line = str.substr(i, next_i - i);
    ImGui::Text("%s", line.c_str());
    i = next_i + 1;
  }
}

void TraceViewer::DrawUI() {
  // ImGui::ShowTestWindow();

  DrawControllerUI();
  DrawCommandListUI();
  DrawStateUI();
  DrawPacketDisassemblerUI();
}

void TraceViewer::DrawControllerUI() {
  ImGui::SetNextWindowPos(ImVec2(5, 5), ImGuiSetCond_FirstUseEver);
  if (!ImGui::Begin("Controller", nullptr, ImVec2(340, 60))) {
    ImGui::End();
    return;
  }

  int target_frame = player_->current_frame_index();
  if (ImGui::Button("|<<")) {
    target_frame = 0;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reset to first frame");
  }
  ImGui::SameLine();
  ImGui::PushButtonRepeat(true);
  if (ImGui::Button(">>", ImVec2(0, 0))) {
    if (target_frame + 1 < player_->frame_count()) {
      ++target_frame;
    }
  }
  ImGui::PopButtonRepeat();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Next frame (hold for continuous)");
  }
  ImGui::SameLine();
  if (ImGui::Button(">>|")) {
    target_frame = player_->frame_count() - 1;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Skip to last frame");
  }
  if (player_->is_playing_trace()) {
    // Don't allow the user to change the frame index just yet...
    // TODO: Find a way to disable the slider below.
    target_frame = player_->current_frame_index();
  }

  ImGui::SameLine();
  ImGui::SliderInt("", &target_frame, 0, player_->frame_count() - 1);
  if (target_frame != player_->current_frame_index() &&
      !player_->is_playing_trace()) {
    player_->SeekFrame(target_frame);
  }
  ImGui::End();
}

void TraceViewer::DrawPacketDisassemblerUI() {
  ImGui::SetNextWindowCollapsed(true, ImGuiSetCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(float(window_->width()) - 500 - 5, 5),
                          ImGuiSetCond_FirstUseEver);
  if (!ImGui::Begin("Packet Disassembler", nullptr, ImVec2(500, 300))) {
    ImGui::End();
    return;
  }
  if (!player_->current_frame() || player_->current_command_index() == -1) {
    ImGui::Text("No frame/command selected");
    ImGui::End();
    return;
  }

  auto frame = player_->current_frame();
  const auto& command = frame->commands[player_->current_command_index()];
  const uint8_t* start_ptr = command.start_ptr;
  const uint8_t* end_ptr = command.end_ptr;

  ImGui::Text("Frame #%d, command %d", player_->current_frame_index(),
              player_->current_command_index());
  ImGui::Separator();
  ImGui::BeginChild("packet_disassembler_list");
  const PacketStartCommand* pending_packet = nullptr;
  auto trace_ptr = start_ptr;
  while (trace_ptr < end_ptr) {
    auto type = static_cast<TraceCommandType>(xe::load<uint32_t>(trace_ptr));
    switch (type) {
      case TraceCommandType::kPrimaryBufferStart: {
        auto cmd =
            reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        ImGui::BulletText("PrimaryBufferStart");
        break;
      }
      case TraceCommandType::kPrimaryBufferEnd: {
        auto cmd = reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        ImGui::BulletText("PrimaryBufferEnd");
        break;
      }
      case TraceCommandType::kIndirectBufferStart: {
        auto cmd =
            reinterpret_cast<const IndirectBufferStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        ImGui::BulletText("IndirectBufferStart");
        break;
      }
      case TraceCommandType::kIndirectBufferEnd: {
        auto cmd = reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        ImGui::BulletText("IndirectBufferEnd");
        break;
      }
      case TraceCommandType::kPacketStart: {
        auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        pending_packet = cmd;
        break;
      }
      case TraceCommandType::kPacketEnd: {
        auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        if (pending_packet) {
          PacketInfo packet_info = {0};
          if (PacketDisassembler::DisasmPacket(
                  reinterpret_cast<const uint8_t*>(pending_packet) +
                      sizeof(PacketStartCommand),
                  &packet_info)) {
            if (packet_info.predicated) {
              ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
            }
            ImGui::BulletText("%s", packet_info.type_info->name);
            ImGui::TreePush((const char*)0);
            for (auto action : packet_info.actions) {
              switch (action.type) {
                case PacketAction::Type::kRegisterWrite: {
                  auto register_info = xe::gpu::RegisterFile::GetRegisterInfo(
                      action.register_write.index);
                  ImGui::Columns(2);
                  ImGui::Text("%.4X %s", action.register_write.index,
                              register_info ? register_info->name : "???");
                  ImGui::NextColumn();
                  if (!register_info ||
                      register_info->type == RegisterInfo::Type::kDword) {
                    ImGui::Text("%.8X", action.register_write.value.u32);
                  } else {
                    ImGui::Text("%8f", action.register_write.value.f32);
                  }
                  ImGui::Columns(1);
                  break;
                }
                case PacketAction::Type::kSetBinMask: {
                  ImGui::Text("%.16" PRIX64, action.set_bin_mask.value);
                  break;
                }
                case PacketAction::Type::kSetBinSelect: {
                  ImGui::Text("%.16" PRIX64, action.set_bin_select.value);
                  break;
                }
              }
            }
            ImGui::TreePop();
            if (packet_info.predicated) {
              ImGui::PopStyleColor();
            }
          } else {
            ImGui::BulletText("<invalid packet>");
          }
          pending_packet = nullptr;
        }
        break;
      }
      case TraceCommandType::kMemoryRead: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->encoded_length;
        // ImGui::BulletText("MemoryRead");
        break;
      }
      case TraceCommandType::kMemoryWrite: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd) + cmd->encoded_length;
        // ImGui::BulletText("MemoryWrite");
        break;
      }
      case TraceCommandType::kEvent: {
        auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        switch (cmd->event_type) {
          case EventCommand::Type::kSwap: {
            ImGui::BulletText("<swap>");
            break;
          }
        }
        break;
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

int TraceViewer::RecursiveDrawCommandBufferUI(
    const TraceReader::Frame* frame, TraceReader::CommandBuffer* buffer) {
  int selected_id = -1;
  int column_width = int(ImGui::GetContentRegionMax().x);

  for (size_t i = 0; i < buffer->commands.size(); i++) {
    switch (buffer->commands[i].type) {
      case TraceReader::CommandBuffer::Command::Type::kBuffer: {
        auto subtree = buffer->commands[i].command_subtree.get();
        if (!subtree->commands.size()) {
          continue;
        }

        ImGui::PushID(int(i));
        if (ImGui::TreeNode((void*)0, "Indirect Buffer %" PRIu64, i)) {
          ImGui::Indent();
          auto id = RecursiveDrawCommandBufferUI(
              frame, buffer->commands[i].command_subtree.get());
          ImGui::Unindent();
          ImGui::TreePop();

          if (id != -1) {
            selected_id = id;
          }
        }
        ImGui::PopID();
      } break;

      case TraceReader::CommandBuffer::Command::Type::kCommand: {
        uint32_t command_id = buffer->commands[i].command_id;

        const auto& command = frame->commands[command_id];
        bool is_selected = command_id == player_->current_command_index();
        const char* label;
        switch (command.type) {
          case TraceReader::Frame::Command::Type::kDraw:
            label = "Draw";
            break;
          case TraceReader::Frame::Command::Type::kSwap:
            label = "Swap";
            break;
        }

        ImGui::PushID(command_id);
        if (ImGui::Selectable(label, &is_selected)) {
          selected_id = command_id;
        }
        ImGui::SameLine(column_width - 60.0f);
        ImGui::Text("%d", command_id);
        ImGui::PopID();
        // if (did_seek && target_command == i) {
        //   ImGui::SetScrollPosHere();
        // }
      } break;
    }
  }

  return selected_id;
}

void TraceViewer::DrawCommandListUI() {
  ImGui::SetNextWindowPos(ImVec2(5, 70), ImGuiSetCond_FirstUseEver);
  if (!ImGui::Begin("Command List", nullptr, ImVec2(200, 640))) {
    ImGui::End();
    return;
  }

  static const TracePlayer::Frame* previous_frame = nullptr;
  auto frame = player_->current_frame();
  if (!frame) {
    ImGui::End();
    return;
  }
  bool did_seek = false;
  if (previous_frame != frame) {
    did_seek = true;
    previous_frame = frame;
  }
  int command_count = int(frame->commands.size());
  int target_command = player_->current_command_index();
  int column_width = int(ImGui::GetContentRegionMax().x);
  ImGui::Text("Frame #%d", player_->current_frame_index());
  ImGui::Separator();
  if (ImGui::Button("reset")) {
    target_command = -1;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reset to before any frame commands");
  }
  ImGui::SameLine();
  ImGui::PushButtonRepeat(true);
  if (ImGui::Button("prev", ImVec2(0, 0))) {
    if (target_command >= 0) {
      --target_command;
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Move to the previous command (hold)");
  }
  ImGui::SameLine();
  if (ImGui::Button("next", ImVec2(0, 0))) {
    if (target_command < command_count - 1) {
      ++target_command;
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Move to the next command (hold)");
  }
  ImGui::PopButtonRepeat();
  ImGui::SameLine();
  if (ImGui::Button("end")) {
    target_command = command_count - 1;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Move to the last command");
  }
  if (player_->is_playing_trace()) {
    // Don't allow the user to change the command index just yet...
    // TODO: Find a way to disable the slider below.
    target_command = player_->current_command_index();
  }

  ImGui::PushItemWidth(float(column_width - 15));
  ImGui::SliderInt("", &target_command, -1, command_count - 1);
  ImGui::PopItemWidth();

  if (target_command != player_->current_command_index() &&
      !player_->is_playing_trace()) {
    did_seek = true;
    player_->SeekCommand(target_command);
  }
  ImGui::Separator();
  ImGui::BeginChild("command_list");
  ImGui::PushID(-1);
  bool is_selected = player_->current_command_index() == -1;
  if (ImGui::Selectable("<start>", &is_selected)) {
    player_->SeekCommand(-1);
  }
  ImGui::PopID();
  if (did_seek && target_command == -1) {
    ImGui::SetScrollPosHere();
  }

  auto id = RecursiveDrawCommandBufferUI(frame, frame->command_tree.get());
  if (id != -1 && id != player_->current_command_index() &&
      !player_->is_playing_trace()) {
    player_->SeekCommand(id);
  }

  ImGui::EndChild();
  ImGui::End();
}

TraceViewer::ShaderDisplayType TraceViewer::DrawShaderTypeUI() {
  static ShaderDisplayType shader_display_type = ShaderDisplayType::kUcode;
  ImGui::RadioButton("ucode", reinterpret_cast<int*>(&shader_display_type),
                     static_cast<int>(ShaderDisplayType::kUcode));
  ImGui::SameLine();
  ImGui::RadioButton("translated", reinterpret_cast<int*>(&shader_display_type),
                     static_cast<int>(ShaderDisplayType::kTranslated));
  ImGui::SameLine();
  ImGui::RadioButton("disasm", reinterpret_cast<int*>(&shader_display_type),
                     static_cast<int>(ShaderDisplayType::kHostDisasm));
  return shader_display_type;
}

void TraceViewer::DrawShaderUI(Shader* shader, ShaderDisplayType display_type) {
  // Must be prepared for advanced display modes.
  if (display_type != ShaderDisplayType::kUcode) {
    if (!shader->is_valid()) {
      ImGui::TextColored(kColorError,
                         "ERROR: shader error during parsing/translation");
      return;
    }
  }

  switch (display_type) {
    case ShaderDisplayType::kUcode: {
      DrawMultilineString(shader->ucode_disassembly());
      break;
    }
    case ShaderDisplayType::kTranslated: {
      const auto& str = shader->GetTranslatedBinaryString();
      size_t i = 0;
      bool done = false;
      while (!done && i < str.size()) {
        size_t next_i = str.find('\n', i);
        if (next_i == std::string::npos) {
          done = true;
          next_i = str.size() - 1;
        }
        auto line = str.substr(i, next_i - i);
        if (line.find("//") != std::string::npos) {
          ImGui::TextColored(kColorComment, "%s", line.c_str());
        } else {
          ImGui::Text("%s", line.c_str());
        }
        i = next_i + 1;
      }
      break;
    }
    case ShaderDisplayType::kHostDisasm: {
      DrawMultilineString(shader->host_disassembly());
      break;
    }
  }
}

// glBlendEquationSeparatei(i, blend_op, blend_op_alpha);
// glBlendFuncSeparatei(i, src_blend, dest_blend, src_blend_alpha,
//  dest_blend_alpha);
void TraceViewer::DrawBlendMode(uint32_t src_blend, uint32_t dest_blend,
                                uint32_t blend_op) {
  static const char* kBlendNames[] = {
      /*  0 */ "ZERO",
      /*  1 */ "ONE",
      /*  2 */ "UNK2",  // ?
      /*  3 */ "UNK3",  // ?
      /*  4 */ "SRC_COLOR",
      /*  5 */ "ONE_MINUS_SRC_COLOR",
      /*  6 */ "SRC_ALPHA",
      /*  7 */ "ONE_MINUS_SRC_ALPHA",
      /*  8 */ "DST_COLOR",
      /*  9 */ "ONE_MINUS_DST_COLOR",
      /* 10 */ "DST_ALPHA",
      /* 11 */ "ONE_MINUS_DST_ALPHA",
      /* 12 */ "CONSTANT_COLOR",
      /* 13 */ "ONE_MINUS_CONSTANT_COLOR",
      /* 14 */ "CONSTANT_ALPHA",
      /* 15 */ "ONE_MINUS_CONSTANT_ALPHA",
      /* 16 */ "SRC_ALPHA_SATURATE",
  };
  const char* src_str = kBlendNames[src_blend];
  const char* dest_str = kBlendNames[dest_blend];
  const char* op_template;
  switch (blend_op) {
    case 0:  // add
      op_template = "%s + %s";
      break;
    case 1:  // subtract
      op_template = "%s - %s";
      break;
    case 2:  // min
      op_template = "min(%s, %s)";
      break;
    case 3:  // max
      op_template = "max(%s, %s)";
      break;
    case 4:  // reverse subtract
      op_template = "-(%s) + %s";
      break;
    default:
      op_template = "%s ? %s";
      break;
  }
  ImGui::Text(op_template, src_str, dest_str);
}

void TraceViewer::DrawTextureInfo(
    const Shader::TextureBinding& texture_binding) {
  auto& regs = *graphics_system_->register_file();

  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
          texture_binding.fetch_constant * 6;
  auto group = reinterpret_cast<const xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;
  if (fetch.type != 0x2) {
    DrawFailedTextureInfo(texture_binding, "Invalid fetch type");
    return;
  }
  TextureInfo texture_info;
  if (!TextureInfo::Prepare(fetch, &texture_info)) {
    DrawFailedTextureInfo(texture_binding,
                          "Unable to parse texture fetcher info");
    return;
  }
  SamplerInfo sampler_info;
  if (!SamplerInfo::Prepare(fetch, texture_binding.fetch_instr,
                            &sampler_info)) {
    DrawFailedTextureInfo(texture_binding, "Unable to parse sampler info");
    return;
  }
  auto texture = GetTextureEntry(texture_info, sampler_info);

  ImGui::Columns(2);
  if (texture) {
    ImVec2 button_size(256, 256);
    if (ImGui::ImageButton(ImTextureID(texture), button_size, ImVec2(0, 0),
                           ImVec2(1, 1))) {
      // show viewer
    }
  } else {
    DrawFailedTextureInfo(texture_binding, "Failed to demand texture");
  }
  ImGui::NextColumn();
  ImGui::Text("Fetch Slot: %u", texture_binding.fetch_constant);
  ImGui::Text("Guest Address: %.8X", texture_info.guest_address);
  ImGui::Text("Format: %s", texture_info.format_info()->name);
  switch (texture_info.dimension) {
    case Dimension::k1D:
      ImGui::Text("1D: %dpx", texture_info.width + 1);
      break;
    case Dimension::k2D:
      ImGui::Text("2D: %dx%dpx", texture_info.width + 1,
                  texture_info.height + 1);
      break;
    case Dimension::k3D:
      ImGui::Text("3D: %dx%dx%dpx", texture_info.width + 1,
                  texture_info.height + 1, texture_info.depth + 1);
      break;
    case Dimension::kCube:
      ImGui::Text("Cube: ?");
      break;
  }
  static const char* kSwizzleMap[] = {"R", "G", "B", "A", "0", "1"};
  ImGui::Text("Swizzle: %s%s%s%s", kSwizzleMap[(fetch.swizzle >> 0) & 0x7],
              kSwizzleMap[(fetch.swizzle >> 3) & 0x7],
              kSwizzleMap[(fetch.swizzle >> 6) & 0x7],
              kSwizzleMap[(fetch.swizzle >> 9) & 0x7]);

  ImGui::Columns(1);
}

void TraceViewer::DrawFailedTextureInfo(
    const Shader::TextureBinding& texture_binding, const char* message) {
  // TODO(benvanik): better error info/etc.
  ImGui::TextColored(kColorError, "ERROR: %s", message);
}

void TraceViewer::DrawVertexFetcher(Shader* shader,
                                    const Shader::VertexBinding& vertex_binding,
                                    const xe_gpu_vertex_fetch_t* fetch) {
  const uint8_t* addr = memory_->TranslatePhysical(fetch->address << 2);
  uint32_t vertex_count = fetch->size / vertex_binding.stride_words;
  int column_count = 0;
  for (const auto& attrib : vertex_binding.attributes) {
    switch (attrib.fetch_instr.attributes.data_format) {
      case VertexFormat::k_32:
      case VertexFormat::k_32_FLOAT:
        ++column_count;
        break;
      case VertexFormat::k_16_16:
      case VertexFormat::k_16_16_FLOAT:
      case VertexFormat::k_32_32:
      case VertexFormat::k_32_32_FLOAT:
        column_count += 2;
        break;
      case VertexFormat::k_10_11_11:
      case VertexFormat::k_11_11_10:
      case VertexFormat::k_32_32_32_FLOAT:
        column_count += 3;
        break;
      case VertexFormat::k_8_8_8_8:
        ++column_count;
        break;
      case VertexFormat::k_2_10_10_10:
      case VertexFormat::k_16_16_16_16:
      case VertexFormat::k_32_32_32_32:
      case VertexFormat::k_16_16_16_16_FLOAT:
      case VertexFormat::k_32_32_32_32_FLOAT:
        column_count += 4;
        break;
      case VertexFormat::kUndefined:
        assert_unhandled_case(attrib.fetch_instr.attributes.data_format);
        break;
    }
  }
  ImGui::BeginChild("#indices", ImVec2(0, 300));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 0));
  int display_start, display_end;
  ImGui::CalcListClipping(vertex_count, ImGui::GetTextLineHeight(),
                          &display_start, &display_end);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                       (display_start)*ImGui::GetTextLineHeight());
  ImGui::Columns(column_count);
  if (display_start <= 1) {
    for (size_t el_index = 0; el_index < vertex_binding.attributes.size();
         ++el_index) {
      const auto& attrib = vertex_binding.attributes[el_index];
      switch (attrib.fetch_instr.attributes.data_format) {
        case VertexFormat::k_32:
        case VertexFormat::k_32_FLOAT:
          ImGui::Text("e%" PRId64 ".x", el_index);
          ImGui::NextColumn();
          break;
        case VertexFormat::k_16_16:
        case VertexFormat::k_16_16_FLOAT:
        case VertexFormat::k_32_32:
        case VertexFormat::k_32_32_FLOAT:
          ImGui::Text("e%" PRId64 ".x", el_index);
          ImGui::NextColumn();
          ImGui::Text("e%" PRId64 ".y", el_index);
          ImGui::NextColumn();
          break;
        case VertexFormat::k_10_11_11:
        case VertexFormat::k_11_11_10:
        case VertexFormat::k_32_32_32_FLOAT:
          ImGui::Text("e%" PRId64 ".x", el_index);
          ImGui::NextColumn();
          ImGui::Text("e%" PRId64 ".y", el_index);
          ImGui::NextColumn();
          ImGui::Text("e%" PRId64 ".z", el_index);
          ImGui::NextColumn();
          break;
        case VertexFormat::k_8_8_8_8:
          ImGui::Text("e%" PRId64 ".xyzw", el_index);
          ImGui::NextColumn();
          break;
        case VertexFormat::k_2_10_10_10:
        case VertexFormat::k_16_16_16_16:
        case VertexFormat::k_32_32_32_32:
        case VertexFormat::k_16_16_16_16_FLOAT:
        case VertexFormat::k_32_32_32_32_FLOAT:
          ImGui::Text("e%" PRId64 ".x", el_index);
          ImGui::NextColumn();
          ImGui::Text("e%" PRId64 ".y", el_index);
          ImGui::NextColumn();
          ImGui::Text("e%" PRId64 ".z", el_index);
          ImGui::NextColumn();
          ImGui::Text("e%" PRId64 ".w", el_index);
          ImGui::NextColumn();
          break;
        case VertexFormat::kUndefined:
          assert_unhandled_case(attrib.fetch_instr.attributes.data_format);
          break;
      }
    }
    ImGui::Separator();
  }
  for (int i = display_start; i < display_end; ++i) {
    const uint8_t* vstart = addr + i * vertex_binding.stride_words * 4;
    for (const auto& attrib : vertex_binding.attributes) {
#define LOADEL(type, wo)                                                   \
  GpuSwap(xe::load<type>(vstart +                                          \
                         (attrib.fetch_instr.attributes.offset + wo) * 4), \
          Endian(fetch->endian))
      switch (attrib.fetch_instr.attributes.data_format) {
        case VertexFormat::k_32:
          ImGui::Text("%.8X", LOADEL(uint32_t, 0));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_32_FLOAT:
          ImGui::Text("%.3f", LOADEL(float, 0));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_16_16: {
          auto e0 = LOADEL(uint32_t, 0);
          ImGui::Text("%.4X", (e0 >> 16) & 0xFFFF);
          ImGui::NextColumn();
          ImGui::Text("%.4X", (e0 >> 0) & 0xFFFF);
          ImGui::NextColumn();
        } break;
        case VertexFormat::k_16_16_FLOAT: {
          auto e0 = LOADEL(uint32_t, 0);
          ImGui::Text("%.2f",
                      half_float::detail::half2float((e0 >> 16) & 0xFFFF));
          ImGui::NextColumn();
          ImGui::Text("%.2f",
                      half_float::detail::half2float((e0 >> 0) & 0xFFFF));
          ImGui::NextColumn();
        } break;
        case VertexFormat::k_32_32:
          ImGui::Text("%.8X", LOADEL(uint32_t, 0));
          ImGui::NextColumn();
          ImGui::Text("%.8X", LOADEL(uint32_t, 1));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_32_32_FLOAT:
          ImGui::Text("%.3f", LOADEL(float, 0));
          ImGui::NextColumn();
          ImGui::Text("%.3f", LOADEL(float, 1));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_10_11_11:
        case VertexFormat::k_11_11_10:
          ImGui::Text("??");
          ImGui::NextColumn();
          ImGui::Text("??");
          ImGui::NextColumn();
          ImGui::Text("??");
          ImGui::NextColumn();
          break;
        case VertexFormat::k_32_32_32_FLOAT:
          ImGui::Text("%.3f", LOADEL(float, 0));
          ImGui::NextColumn();
          ImGui::Text("%.3f", LOADEL(float, 1));
          ImGui::NextColumn();
          ImGui::Text("%.3f", LOADEL(float, 2));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_8_8_8_8:
          ImGui::Text("%.8X", LOADEL(uint32_t, 0));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_2_10_10_10: {
          auto e0 = LOADEL(uint32_t, 0);
          ImGui::Text("??");
          ImGui::NextColumn();
          ImGui::Text("??");
          ImGui::NextColumn();
          ImGui::Text("??");
          ImGui::NextColumn();
          ImGui::Text("??");
          ImGui::NextColumn();
        } break;
        case VertexFormat::k_16_16_16_16: {
          auto e0 = LOADEL(uint32_t, 0);
          auto e1 = LOADEL(uint32_t, 1);
          ImGui::Text("%.4X", (e0 >> 16) & 0xFFFF);
          ImGui::NextColumn();
          ImGui::Text("%.4X", (e0 >> 0) & 0xFFFF);
          ImGui::NextColumn();
          ImGui::Text("%.4X", (e1 >> 16) & 0xFFFF);
          ImGui::NextColumn();
          ImGui::Text("%.4X", (e1 >> 0) & 0xFFFF);
          ImGui::NextColumn();
        } break;
        case VertexFormat::k_32_32_32_32:
          ImGui::Text("%.8X", LOADEL(uint32_t, 0));
          ImGui::NextColumn();
          ImGui::Text("%.8X", LOADEL(uint32_t, 1));
          ImGui::NextColumn();
          ImGui::Text("%.8X", LOADEL(uint32_t, 2));
          ImGui::NextColumn();
          ImGui::Text("%.8X", LOADEL(uint32_t, 3));
          ImGui::NextColumn();
          break;
        case VertexFormat::k_16_16_16_16_FLOAT: {
          auto e0 = LOADEL(uint32_t, 0);
          auto e1 = LOADEL(uint32_t, 1);
          ImGui::Text("%.2f",
                      half_float::detail::half2float((e0 >> 16) & 0xFFFF));
          ImGui::NextColumn();
          ImGui::Text("%.2f",
                      half_float::detail::half2float((e0 >> 0) & 0xFFFF));
          ImGui::NextColumn();
          ImGui::Text("%.2f",
                      half_float::detail::half2float((e1 >> 16) & 0xFFFF));
          ImGui::NextColumn();
          ImGui::Text("%.2f",
                      half_float::detail::half2float((e1 >> 0) & 0xFFFF));
          ImGui::NextColumn();
        } break;
        case VertexFormat::k_32_32_32_32_FLOAT:
          ImGui::Text("%.3f", LOADEL(float, 0));
          ImGui::NextColumn();
          ImGui::Text("%.3f", LOADEL(float, 1));
          ImGui::NextColumn();
          ImGui::Text("%.3f", LOADEL(float, 2));
          ImGui::NextColumn();
          ImGui::Text("%.3f", LOADEL(float, 3));
          ImGui::NextColumn();
          break;
        case VertexFormat::kUndefined:
          assert_unhandled_case(attrib.fetch_instr.attributes.data_format);
          break;
      }
    }
  }
  ImGui::Columns(1);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (vertex_count - display_end) *
                                                    ImGui::GetTextLineHeight());
  ImGui::PopStyleVar();
  ImGui::EndChild();
}

static const char* kCompareFuncNames[] = {
    "<false>", "<", "==", "<=", ">", "!=", ">=", "<true>",
};
static const char* kStencilFuncNames[] = {
    "Keep",
    "Zero",
    "Replace",
    "Increment and Wrap",
    "Decrement and Wrap",
    "Invert",
    "Increment and Clamp",
    "Decrement and Clamp",
};
static const char* kIndexFormatNames[] = {
    "uint16",
    "uint32",
};
static const char* kEndiannessNames[] = {
    "unspecified endianness",
    "8-in-16",
    "8-in-32",
    "16-in-32",
};
static const char* kColorFormatNames[] = {
    /* 0  */ "k_8_8_8_8",
    /* 1  */ "k_8_8_8_8_GAMMA",
    /* 2  */ "k_2_10_10_10",
    /* 3  */ "k_2_10_10_10_FLOAT",
    /* 4  */ "k_16_16",
    /* 5  */ "k_16_16_16_16",
    /* 6  */ "k_16_16_FLOAT",
    /* 7  */ "k_16_16_16_16_FLOAT",
    /* 8  */ "unknown(8)",
    /* 9  */ "unknown(9)",
    /* 10 */ "k_2_10_10_10_unknown",
    /* 11 */ "unknown(11)",
    /* 12 */ "k_2_10_10_10_FLOAT_unknown",
    /* 13 */ "unknown(13)",
    /* 14 */ "k_32_FLOAT",
    /* 15 */ "k_32_32_FLOAT",
};
static const char* kDepthFormatNames[] = {
    "kD24S8",
    "kD24FS8",
};

void ProgressBar(float frac, float width, float height = 0,
                 const ImVec4& color = ImVec4(0, 1, 0, 1),
                 const ImVec4& border_color = ImVec4(0, 1, 0, 1)) {
  if (height == 0) {
    height = ImGui::GetTextLineHeightWithSpacing();
  }
  frac = xe::saturate(frac);

  const auto fontAtlas = ImGui::GetIO().Fonts;

  auto pos = ImGui::GetCursorScreenPos();
  auto col = ImGui::ColorConvertFloat4ToU32(color);
  auto border_col = ImGui::ColorConvertFloat4ToU32(border_color);

  if (frac > 0) {
    // Progress bar
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos, ImVec2(pos.x + width * frac, pos.y + height), col);
  }
  if (border_color.w > 0.f) {
    // Border
    ImGui::GetWindowDrawList()->AddRect(
        pos, ImVec2(pos.x + width, pos.y + height), border_col);
  }

  ImGui::Dummy(ImVec2(width, height));
}

void ZoomedImage(ImTextureID tex, ImVec2 rel_pos, ImVec2 tex_size,
                 float focus_size, ImVec2 image_size = ImVec2(128, 128)) {
  ImVec2 focus;
  focus.x = rel_pos.x - (focus_size * 0.5f);
  focus.y = rel_pos.y - (focus_size * 0.5f);

  ImVec2 uv0 = ImVec2(focus.x / tex_size.x, focus.y / tex_size.y);
  ImVec2 uv1 = ImVec2((focus.x + focus_size) / tex_size.x,
                      (focus.y + focus_size) / tex_size.y);
  ImGui::Image(tex, image_size, uv0, uv1);
}

void TraceViewer::DrawStateUI() {
  auto command_processor = graphics_system_->command_processor();
  auto& regs = *graphics_system_->register_file();

  ImGui::SetNextWindowPos(ImVec2(float(window_->width()) - 500 - 5, 30),
                          ImGuiSetCond_FirstUseEver);
  if (!ImGui::Begin("State", nullptr, ImVec2(500, 680))) {
    ImGui::End();
    return;
  }

  if (!player_->current_frame() || player_->current_command_index() == -1) {
    ImGui::Text("No frame/command selected");
    ImGui::End();
    return;
  }

  auto frame = player_->current_frame();
  const auto& command = frame->commands[player_->current_command_index()];
  auto packet_head = command.head_ptr + sizeof(PacketStartCommand);
  uint32_t packet = xe::load_and_swap<uint32_t>(packet_head);
  uint32_t packet_type = packet >> 30;
  assert_true(packet_type == 0x03);
  uint32_t opcode = (packet >> 8) & 0x7F;
  struct {
    PrimitiveType prim_type;
    bool is_auto_index;
    uint32_t index_count;
    uint32_t index_buffer_ptr;
    uint32_t index_buffer_size;
    Endian index_endianness;
    IndexFormat index_format;
  } draw_info;
  std::memset(&draw_info, 0, sizeof(draw_info));
  switch (opcode) {
    case PM4_DRAW_INDX: {
      uint32_t dword0 = xe::load_and_swap<uint32_t>(packet_head + 4);
      uint32_t dword1 = xe::load_and_swap<uint32_t>(packet_head + 8);
      draw_info.index_count = dword1 >> 16;
      draw_info.prim_type = static_cast<PrimitiveType>(dword1 & 0x3F);
      uint32_t src_sel = (dword1 >> 6) & 0x3;
      if (src_sel == 0x0) {
        // Indexed draw.
        draw_info.is_auto_index = false;
        draw_info.index_buffer_ptr =
            xe::load_and_swap<uint32_t>(packet_head + 12);
        uint32_t index_size = xe::load_and_swap<uint32_t>(packet_head + 16);
        draw_info.index_endianness = static_cast<Endian>(index_size >> 30);
        index_size &= 0x00FFFFFF;
        bool index_32bit = (dword1 >> 11) & 0x1;
        draw_info.index_format =
            index_32bit ? IndexFormat::kInt32 : IndexFormat::kInt16;
        draw_info.index_buffer_size = index_size * (index_32bit ? 4 : 2);
      } else if (src_sel == 0x2) {
        // Auto draw.
        draw_info.is_auto_index = true;
      } else {
        // Unknown source select.
        assert_always();
      }
      break;
    }
    case PM4_DRAW_INDX_2: {
      uint32_t dword0 = xe::load_and_swap<uint32_t>(packet_head + 4);
      uint32_t src_sel = (dword0 >> 6) & 0x3;
      assert_true(src_sel == 0x2);  // 'SrcSel=AutoIndex'
      draw_info.prim_type = static_cast<PrimitiveType>(dword0 & 0x3F);
      draw_info.is_auto_index = true;
      draw_info.index_count = dword0 >> 16;
      break;
    }
  }

  if (player_->is_playing_trace()) {
    ImGui::Text("Playing trace...");
    float width = ImGui::GetWindowWidth() - 20.f;

    ProgressBar(float(player_->playback_percent()) / 10000.f, width);
    ImGui::End();
    return;
  }

  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);

  const char* mode_name = "Unknown";
  switch (enable_mode) {
    case ModeControl::kIgnore:
      ImGui::Text("Ignored Command %d", player_->current_command_index());
      break;
    case ModeControl::kColorDepth:
    case ModeControl::kDepth: {
      static const char* kPrimNames[] = {
          "<none>",         "point list",   "line list",      "line strip",
          "triangle list",  "triangle fan", "triangle strip", "unknown 0x7",
          "rectangle list", "unknown 0x9",  "unknown 0xA",    "unknown 0xB",
          "line loop",      "quad list",    "quad strip",     "unknown 0xF",
      };
      ImGui::Text("%s Command %d: %s, %d indices",
                  enable_mode == ModeControl::kColorDepth ? "Color-Depth"
                                                          : "Depth-only",
                  player_->current_command_index(),
                  kPrimNames[int(draw_info.prim_type)], draw_info.index_count);
      break;
    }
    case ModeControl::kCopy: {
      uint32_t copy_dest_base = regs[XE_GPU_REG_RB_COPY_DEST_BASE].u32;
      ImGui::Text("Copy Command %d (to %.8X)", player_->current_command_index(),
                  copy_dest_base);
      break;
    }
  }

  ImGui::Columns(2);
  ImGui::BulletText("Viewport State:");
  if (true) {
    ImGui::TreePush((const void*)0);
    uint32_t pa_su_sc_mode_cntl = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;
    if ((pa_su_sc_mode_cntl >> 16) & 1) {
      uint32_t window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
      int16_t window_offset_x = window_offset & 0x7FFF;
      int16_t window_offset_y = (window_offset >> 16) & 0x7FFF;
      if (window_offset_x & 0x4000) {
        window_offset_x |= 0x8000;
      }
      if (window_offset_y & 0x4000) {
        window_offset_y |= 0x8000;
      }
      ImGui::BulletText("Window Offset: %d, %d", window_offset_x,
                        window_offset_y);
    } else {
      ImGui::BulletText("Window Offset: disabled");
    }
    uint32_t window_scissor_tl = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL].u32;
    uint32_t window_scissor_br = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR].u32;
    ImGui::BulletText(
        "Window Scissor: %d,%d to %d,%d (%d x %d)", window_scissor_tl & 0x7FFF,
        (window_scissor_tl >> 16) & 0x7FFF, window_scissor_br & 0x7FFF,
        (window_scissor_br >> 16) & 0x7FFF,
        (window_scissor_br & 0x7FFF) - (window_scissor_tl & 0x7FFF),
        ((window_scissor_br >> 16) & 0x7FFF) -
            ((window_scissor_tl >> 16) & 0x7FFF));
    uint32_t surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
    uint32_t surface_hiz = (surface_info >> 18) & 0x3FFF;
    uint32_t surface_pitch = surface_info & 0x3FFF;
    auto surface_msaa = (surface_info >> 16) & 0x3;
    static const char* kMsaaNames[] = {
        "1X",
        "2X",
        "4X",
    };
    ImGui::BulletText("Surface Pitch: %d", surface_pitch);
    ImGui::BulletText("Surface HI-Z Pitch: %d", surface_hiz);
    ImGui::BulletText("Surface MSAA: %s", kMsaaNames[surface_msaa]);
    uint32_t vte_control = regs[XE_GPU_REG_PA_CL_VTE_CNTL].u32;
    bool vport_xscale_enable = (vte_control & (1 << 0)) > 0;
    bool vport_xoffset_enable = (vte_control & (1 << 1)) > 0;
    bool vport_yscale_enable = (vte_control & (1 << 2)) > 0;
    bool vport_yoffset_enable = (vte_control & (1 << 3)) > 0;
    bool vport_zscale_enable = (vte_control & (1 << 4)) > 0;
    bool vport_zoffset_enable = (vte_control & (1 << 5)) > 0;
    assert_true(vport_xscale_enable == vport_yscale_enable ==
                vport_zscale_enable == vport_xoffset_enable ==
                vport_yoffset_enable == vport_zoffset_enable);
    if (!vport_xscale_enable) {
      ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
    }
    ImGui::BulletText(
        "Viewport Offset: %f, %f, %f",
        vport_xoffset_enable ? regs[XE_GPU_REG_PA_CL_VPORT_XOFFSET].f32 : 0,
        vport_yoffset_enable ? regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET].f32 : 0,
        vport_zoffset_enable ? regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET].f32 : 0);
    ImGui::BulletText(
        "Viewport Scale: %f, %f, %f",
        vport_xscale_enable ? regs[XE_GPU_REG_PA_CL_VPORT_XSCALE].f32 : 1,
        vport_yscale_enable ? regs[XE_GPU_REG_PA_CL_VPORT_YSCALE].f32 : 1,
        vport_zscale_enable ? regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE].f32 : 1);
    if (!vport_xscale_enable) {
      ImGui::PopStyleColor();
    }

    ImGui::BulletText("Vertex Format: %s, %s, %s, %s",
                      ((vte_control >> 8) & 0x1) ? "x/w0" : "x",
                      ((vte_control >> 8) & 0x1) ? "y/w0" : "y",
                      ((vte_control >> 9) & 0x1) ? "z/w0" : "z",
                      ((vte_control >> 10) & 0x1) ? "w0" : "1/w0");
    uint32_t clip_control = regs[XE_GPU_REG_PA_CL_CLIP_CNTL].u32;
    bool clip_enabled = ((clip_control >> 17) & 0x1) == 0;
    bool dx_clip = ((clip_control >> 20) & 0x1) == 0x1;
    ImGui::BulletText("Clip Enabled: %s, DX Clip: %s",
                      clip_enabled ? "true" : "false",
                      dx_clip ? "true" : "false");
    ImGui::TreePop();
  }
  ImGui::NextColumn();
  ImGui::BulletText("Rasterizer State:");
  if (true) {
    ImGui::TreePush((const void*)0);
    uint32_t pa_su_sc_mode_cntl = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;
    uint32_t pa_sc_screen_scissor_tl =
        regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL].u32;
    uint32_t pa_sc_screen_scissor_br =
        regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR].u32;
    if (pa_sc_screen_scissor_tl != 0 && pa_sc_screen_scissor_br != 0x20002000) {
      int32_t screen_scissor_x = pa_sc_screen_scissor_tl & 0x7FFF;
      int32_t screen_scissor_y = (pa_sc_screen_scissor_tl >> 16) & 0x7FFF;
      int32_t screen_scissor_w =
          (pa_sc_screen_scissor_br & 0x7FFF) - screen_scissor_x;
      int32_t screen_scissor_h =
          ((pa_sc_screen_scissor_br >> 16) & 0x7FFF) - screen_scissor_y;
      ImGui::BulletText("Scissor: %d,%d to %d,%d (%d x %d)", screen_scissor_x,
                        screen_scissor_y, screen_scissor_x + screen_scissor_w,
                        screen_scissor_y + screen_scissor_h, screen_scissor_w,
                        screen_scissor_h);
    } else {
      ImGui::BulletText("Scissor: disabled");
    }
    switch (pa_su_sc_mode_cntl & 0x3) {
      case 0:
        ImGui::BulletText("Culling: disabled");
        break;
      case 1:
        ImGui::BulletText("Culling: front-face");
        break;
      case 2:
        ImGui::BulletText("Culling: back-face");
        break;
    }
    if (pa_su_sc_mode_cntl & 0x4) {
      ImGui::BulletText("Front-face: clockwise");
    } else {
      ImGui::BulletText("Front-face: counter-clockwise");
    }
    static const char* kFillModeNames[3] = {
        "point",
        "line",
        "fill",
    };
    bool poly_mode = ((pa_su_sc_mode_cntl >> 3) & 0x3) != 0;
    if (poly_mode) {
      uint32_t front_poly_mode = (pa_su_sc_mode_cntl >> 5) & 0x7;
      uint32_t back_poly_mode = (pa_su_sc_mode_cntl >> 8) & 0x7;
      // GL only supports both matching.
      assert_true(front_poly_mode == back_poly_mode);
      ImGui::BulletText("Polygon Mode: %s", kFillModeNames[front_poly_mode]);
    } else {
      ImGui::BulletText("Polygon Mode: fill");
    }
    if (pa_su_sc_mode_cntl & (1 << 19)) {
      ImGui::BulletText("Provoking Vertex: last");
    } else {
      ImGui::BulletText("Provoking Vertex: first");
    }
    ImGui::TreePop();
  }
  ImGui::Columns(1);

  auto rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = rb_surface_info & 0x3FFF;
  auto surface_msaa = static_cast<MsaaSamples>((rb_surface_info >> 16) & 0x3);

  if (ImGui::CollapsingHeader("Color Targets")) {
    if (enable_mode != ModeControl::kDepth) {
      // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
      // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
      uint32_t color_control = regs[XE_GPU_REG_RB_COLORCONTROL].u32;
      if ((color_control & 0x8) != 0) {
        ImGui::BulletText("Alpha Test: %s %.2f",
                          kCompareFuncNames[color_control & 0x7],
                          regs[XE_GPU_REG_RB_ALPHA_REF].f32);
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
        ImGui::BulletText("Alpha Test: disabled");
        ImGui::PopStyleColor();
      }

      auto blend_color = ImVec4(regs[XE_GPU_REG_RB_BLEND_RED].f32,
                                regs[XE_GPU_REG_RB_BLEND_GREEN].f32,
                                regs[XE_GPU_REG_RB_BLEND_BLUE].f32,
                                regs[XE_GPU_REG_RB_BLEND_ALPHA].f32);
      ImGui::BulletText("Blend Color: (%.2f,%.2f,%.2f,%.2f)", blend_color.x,
                        blend_color.y, blend_color.z, blend_color.w);
      ImGui::SameLine();
      ImGui::ColorButton(blend_color, true);

      uint32_t rb_color_mask = regs[XE_GPU_REG_RB_COLOR_MASK].u32;
      uint32_t color_info[4] = {
          regs[XE_GPU_REG_RB_COLOR_INFO].u32,
          regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
          regs[XE_GPU_REG_RB_COLOR2_INFO].u32,
          regs[XE_GPU_REG_RB_COLOR3_INFO].u32,
      };
      uint32_t rb_blendcontrol[4] = {
          regs[XE_GPU_REG_RB_BLENDCONTROL_0].u32,
          regs[XE_GPU_REG_RB_BLENDCONTROL_1].u32,
          regs[XE_GPU_REG_RB_BLENDCONTROL_2].u32,
          regs[XE_GPU_REG_RB_BLENDCONTROL_3].u32,
      };
      ImGui::Columns(2);
      for (int i = 0; i < xe::countof(color_info); ++i) {
        uint32_t blend_control = rb_blendcontrol[i];
        // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
        auto src_blend = (blend_control & 0x0000001F) >> 0;
        // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
        auto dest_blend = (blend_control & 0x00001F00) >> 8;
        // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
        auto blend_op = (blend_control & 0x000000E0) >> 5;
        // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
        auto src_blend_alpha = (blend_control & 0x001F0000) >> 16;
        // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
        auto dest_blend_alpha = (blend_control & 0x1F000000) >> 24;
        // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
        auto blend_op_alpha = (blend_control & 0x00E00000) >> 21;
        // A2XX_RB_COLORCONTROL_BLEND_DISABLE ?? Can't find this!
        // Just guess based on actions.
        bool blend_enable = !((src_blend == 1) && (dest_blend == 0) &&
                              (blend_op == 0) && (src_blend_alpha == 1) &&
                              (dest_blend_alpha == 0) && (blend_op_alpha == 0));
        if (blend_enable) {
          if (src_blend == src_blend_alpha && dest_blend == dest_blend_alpha &&
              blend_op == blend_op_alpha) {
            ImGui::BulletText("Blend %d: ", i);
            ImGui::SameLine();
            DrawBlendMode(src_blend, dest_blend, blend_op);
          } else {
            ImGui::BulletText("Blend %d:", i);
            ImGui::BulletText("  Color: ");
            ImGui::SameLine();
            DrawBlendMode(src_blend, dest_blend, blend_op);
            ImGui::BulletText("  Alpha: ");
            ImGui::SameLine();
            DrawBlendMode(src_blend_alpha, dest_blend_alpha, blend_op_alpha);
          }
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
          ImGui::BulletText("Blend %d: disabled", i);
          ImGui::PopStyleColor();
        }
        ImGui::NextColumn();
        uint32_t write_mask = (rb_color_mask >> (i * 4)) & 0xF;
        if (write_mask) {
          ImGui::BulletText("Write Mask %d: %s, %s, %s, %s", i,
                            !!(write_mask & 0x1) ? "true" : "false",
                            !!(write_mask & 0x2) ? "true" : "false",
                            !!(write_mask & 0x4) ? "true" : "false",
                            !!(write_mask & 0x8) ? "true" : "false");
        } else {
          ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
          ImGui::BulletText("Write Mask %d: disabled", i);
          ImGui::PopStyleColor();
        }
        ImGui::NextColumn();
      }
      ImGui::Columns(1);

      ImGui::Columns(4);
      for (int i = 0; i < xe::countof(color_info); ++i) {
        uint32_t write_mask = (rb_color_mask >> (i * 4)) & 0xF;
        uint32_t color_base = color_info[i] & 0xFFF;
        auto color_format =
            static_cast<ColorRenderTargetFormat>((color_info[i] >> 16) & 0xF);
        ImVec2 button_pos = ImGui::GetCursorScreenPos();
        ImVec2 button_size(256, 256);
        ImTextureID tex = 0;
        if (write_mask) {
          auto color_target = GetColorRenderTarget(surface_pitch, surface_msaa,
                                                   color_base, color_format);
          tex = ImTextureID(color_target);
          if (ImGui::ImageButton(tex, button_size, ImVec2(0, 0),
                                 ImVec2(1, 1))) {
            // show viewer
          }
        } else {
          ImGui::ImageButton(ImTextureID(0), button_size, ImVec2(0, 0),
                             ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0),
                             ImVec4(0, 0, 0, 0));
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text("Color Target %d (%s), base %.4X, pitch %d, format %s", i,
                      write_mask ? "enabled" : "disabled", color_base,
                      surface_pitch, kColorFormatNames[uint32_t(color_format)]);

          if (tex) {
            ImVec2 rel_pos;
            rel_pos.x = ImGui::GetMousePos().x - button_pos.x;
            rel_pos.y = ImGui::GetMousePos().y - button_pos.y;
            ZoomedImage(tex, rel_pos, button_size, 32.f, ImVec2(256, 256));
          }

          ImGui::EndTooltip();
        }
        ImGui::NextColumn();
      }
      ImGui::Columns(1);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
      ImGui::Text("Depth-only mode, no color targets");
      ImGui::PopStyleColor();
    }
  }

  if (ImGui::CollapsingHeader("Depth/Stencil Target")) {
    auto rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL].u32;
    auto rb_stencilrefmask = regs[XE_GPU_REG_RB_STENCILREFMASK].u32;
    auto rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
    bool uses_depth =
        (rb_depthcontrol & 0x00000002) || (rb_depthcontrol & 0x00000004);
    uint32_t stencil_ref = (rb_stencilrefmask & 0xFF);
    uint32_t stencil_read_mask = (rb_stencilrefmask >> 8) & 0xFF;
    uint32_t stencil_write_mask = (rb_stencilrefmask >> 16) & 0xFF;
    bool uses_stencil =
        (rb_depthcontrol & 0x00000001) || (stencil_write_mask != 0);

    ImGui::Columns(2);

    if (rb_depthcontrol & 0x00000002) {
      ImGui::BulletText("Depth Test: enabled");
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
      ImGui::BulletText("Depth Test: disabled");
    }
    ImGui::BulletText("Depth Func: %s",
                      kCompareFuncNames[(rb_depthcontrol & 0x00000070) >> 4]);
    if (!(rb_depthcontrol & 0x00000002)) {
      ImGui::PopStyleColor();
    }
    if (rb_depthcontrol & 0x00000004) {
      ImGui::BulletText("Depth Write: enabled");
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
      ImGui::BulletText("Depth Write: disabled");
      ImGui::PopStyleColor();
    }

    if (rb_depthcontrol & 0x00000001) {
      ImGui::BulletText("Stencil Test: enabled");
      ImGui::BulletText("Stencil ref: 0x%.2X", stencil_ref);
      ImGui::BulletText("Stencil read / write masks: 0x%.2X / 0x%.2X",
                        stencil_read_mask, stencil_write_mask);
      ImGui::BulletText("Front State:");
      ImGui::Indent();
      ImGui::BulletText("Compare Op: %s",
                        kCompareFuncNames[(rb_depthcontrol >> 8) & 0x7]);
      ImGui::BulletText("Fail Op: %s",
                        kStencilFuncNames[(rb_depthcontrol >> 11) & 0x7]);
      ImGui::BulletText("Pass Op: %s",
                        kStencilFuncNames[(rb_depthcontrol >> 14) & 0x7]);
      ImGui::BulletText("Depth Fail Op: %s",
                        kStencilFuncNames[(rb_depthcontrol >> 17) & 0x7]);
      ImGui::Unindent();

      // BACKFACE_ENABLE
      if (!(rb_depthcontrol & 0x80)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
        ImGui::BulletText("Back State (same as front)");
        ImGui::PopStyleColor();
      } else {
        ImGui::BulletText("Back State:");
        ImGui::Indent();
        ImGui::BulletText("Compare Op: %s",
                          kCompareFuncNames[(rb_depthcontrol >> 20) & 0x7]);
        ImGui::BulletText("Fail Op: %s",
                          kStencilFuncNames[(rb_depthcontrol >> 23) & 0x7]);
        ImGui::BulletText("Pass Op: %s",
                          kStencilFuncNames[(rb_depthcontrol >> 26) & 0x7]);
        ImGui::BulletText("Depth Fail Op: %s",
                          kStencilFuncNames[(rb_depthcontrol >> 29) & 0x7]);
        ImGui::Unindent();
      }
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, kColorIgnored);
      ImGui::BulletText("Stencil Test: disabled");
      ImGui::PopStyleColor();
    }

    ImGui::NextColumn();

    if (uses_depth || uses_stencil) {
      uint32_t depth_base = rb_depth_info & 0xFFF;
      auto depth_format =
          static_cast<DepthRenderTargetFormat>((rb_depth_info >> 16) & 0x1);
      auto depth_target = GetDepthRenderTarget(surface_pitch, surface_msaa,
                                               depth_base, depth_format);

      auto button_pos = ImGui::GetCursorScreenPos();
      ImVec2 button_size(256, 256);
      ImGui::ImageButton(ImTextureID(depth_target), button_size, ImVec2(0, 0),
                         ImVec2(1, 1));
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();

        ImGui::Text("Depth Target: base %.4X, pitch %d, format %s", depth_base,
                    surface_pitch, kDepthFormatNames[uint32_t(depth_format)]);

        ImVec2 rel_pos;
        rel_pos.x = ImGui::GetMousePos().x - button_pos.x;
        rel_pos.y = ImGui::GetMousePos().y - button_pos.y;
        ZoomedImage(ImTextureID(depth_target), rel_pos, button_size, 32.f,
                    ImVec2(256, 256));

        ImGui::EndTooltip();
      }
    } else {
      ImGui::Text("No depth target");
    }

    ImGui::Columns(1);
  }

  if (ImGui::CollapsingHeader("Vertex Shader")) {
    ShaderDisplayType shader_display_type = DrawShaderTypeUI();
    ImGui::BeginChild("#vertex_shader_text", ImVec2(0, 400));
    auto shader = command_processor->active_vertex_shader();
    if (shader) {
      DrawShaderUI(shader, shader_display_type);
    } else {
      ImGui::TextColored(kColorError, "ERROR: no vertex shader set");
    }
    ImGui::EndChild();
  }
  if (ImGui::CollapsingHeader("Vertex Shader Output") &&
      QueryVSOutputElementSize()) {
    auto size = QueryVSOutputSize();
    auto el_size = QueryVSOutputElementSize();
    if (size > 0) {
      std::vector<float> vertices;
      vertices.resize(size / 4);
      QueryVSOutput(vertices.data(), size);

      ImGui::Text("%" PRIu64 " output vertices", vertices.size() / 4);
      ImGui::SameLine();
      static bool normalize = false;
      ImGui::Checkbox("Normalize", &normalize);

      ImGui::BeginChild("#vsvertices", ImVec2(0, 300));

      int display_start, display_end;
      ImGui::CalcListClipping(int(vertices.size() / 4),
                              ImGui::GetTextLineHeight(), &display_start,
                              &display_end);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           (display_start)*ImGui::GetTextLineHeight());

      ImGui::Columns(int(el_size), "#vsvertices", true);
      for (size_t i = display_start; i < display_end; i++) {
        size_t start_vtx = i * el_size;
        float verts[4] = {vertices[start_vtx], vertices[start_vtx + 1],
                          vertices[start_vtx + 2], vertices[start_vtx + 3]};
        assert_true(el_size <= xe::countof(verts));
        if (normalize) {
          for (int j = 0; j < el_size; j++) {
            verts[j] /= verts[3];
          }
        }

        for (int j = 0; j < el_size; j++) {
          ImGui::Text("%.3f", verts[j]);
          ImGui::NextColumn();
        }
      }
      ImGui::Columns(1);

      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           ((vertices.size() / 4) - display_end) *
                               ImGui::GetTextLineHeight());
      ImGui::EndChild();
    } else {
      ImGui::Text("No vertex shader output");
    }
  }
  if (ImGui::CollapsingHeader("Pixel Shader")) {
    ShaderDisplayType shader_display_type = DrawShaderTypeUI();
    ImGui::BeginChild("#pixel_shader_text", ImVec2(0, 400));
    auto shader = command_processor->active_pixel_shader();
    if (shader) {
      DrawShaderUI(shader, shader_display_type);
    } else {
      ImGui::TextColored(kColorError, "ERROR: no pixel shader set");
    }
    ImGui::EndChild();
  }
  if (ImGui::CollapsingHeader("Index Buffer")) {
    if (draw_info.is_auto_index) {
      ImGui::Text("%d indices, auto-indexed", draw_info.index_count);
    } else {
      ImGui::Text("%d indices from buffer %.8X (%db), %s, %s",
                  draw_info.index_count, draw_info.index_buffer_ptr,
                  draw_info.index_buffer_size,
                  kIndexFormatNames[int(draw_info.index_format)],
                  kEndiannessNames[int(draw_info.index_endianness)]);
      uint32_t pa_su_sc_mode_cntl = regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL].u32;
      if (pa_su_sc_mode_cntl & (1 << 21)) {
        uint32_t reset_index =
            regs[XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX].u32;
        if (draw_info.index_format == IndexFormat::kInt16) {
          ImGui::Text("Reset Index: %.4X", reset_index & 0xFFFF);
        } else {
          ImGui::Text("Reset Index: %.8X", reset_index);
        }
      } else {
        ImGui::Text("Reset Index: disabled");
      }
      ImGui::BeginChild("#indices", ImVec2(0, 300));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
      int display_start, display_end;
      ImGui::CalcListClipping(1 + draw_info.index_count,
                              ImGui::GetTextLineHeight(), &display_start,
                              &display_end);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           (display_start)*ImGui::GetTextLineHeight());
      ImGui::Columns(2, "#indices", true);
      ImGui::SetColumnOffset(1, 60);
      if (display_start <= 1) {
        ImGui::Text("Ordinal");
        ImGui::NextColumn();
        ImGui::Text(" Value");
        ImGui::NextColumn();
        ImGui::Separator();
      }
      uint32_t element_size =
          draw_info.index_format == IndexFormat::kInt32 ? 4 : 2;
      const uint8_t* data_ptr = memory_->TranslatePhysical(
          draw_info.index_buffer_ptr + (display_start * element_size));
      for (int i = display_start; i < display_end;
           ++i, data_ptr += element_size) {
        if (i < 10) {
          ImGui::Text("     %d", i);
        } else if (i < 100) {
          ImGui::Text("    %d", i);
        } else if (i < 1000) {
          ImGui::Text("   %d", i);
        } else {
          ImGui::Text("  %d", i);
        }
        ImGui::NextColumn();
        uint32_t value = element_size == 4
                             ? GpuSwap(xe::load<uint32_t>(data_ptr),
                                       draw_info.index_endianness)
                             : GpuSwap(xe::load<uint16_t>(data_ptr),
                                       draw_info.index_endianness);
        ImGui::Text(" %d", value);
        ImGui::NextColumn();
      }
      ImGui::Columns(1);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           (draw_info.index_count - display_end) *
                               ImGui::GetTextLineHeight());
      ImGui::PopStyleVar();
      ImGui::EndChild();
    }
  }
  if (ImGui::CollapsingHeader("Vertex Buffers")) {
    auto shader = command_processor->active_vertex_shader();
    if (shader) {
      for (const auto& vertex_binding : shader->vertex_bindings()) {
        int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                (vertex_binding.fetch_constant / 3) * 6;
        const auto group =
            reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
        const xe_gpu_vertex_fetch_t* fetch = nullptr;
        switch (vertex_binding.fetch_constant % 3) {
          case 0:
            fetch = &group->vertex_fetch_0;
            break;
          case 1:
            fetch = &group->vertex_fetch_1;
            break;
          case 2:
            fetch = &group->vertex_fetch_2;
            break;
        }
        assert_true(fetch->endian == 2);
        char tree_root_id[32];
        sprintf(tree_root_id, "#vertices_root_%d",
                vertex_binding.fetch_constant);
        if (ImGui::TreeNode(tree_root_id, "vf%d: 0x%.8X (%db), %s",
                            vertex_binding.fetch_constant, fetch->address << 2,
                            fetch->size * 4,
                            kEndiannessNames[int(fetch->endian)])) {
          ImGui::BeginChild("#vertices", ImVec2(0, 300));
          DrawVertexFetcher(shader, vertex_binding, fetch);
          ImGui::EndChild();
          ImGui::TreePop();
        }
      }
    } else {
      ImGui::TextColored(kColorError, "ERROR: no vertex shader set");
    }
  }
  if (ImGui::CollapsingHeader("Vertex Textures")) {
    auto shader = command_processor->active_vertex_shader();
    if (shader) {
      const auto& texture_bindings = shader->texture_bindings();
      if (!texture_bindings.empty()) {
        for (const auto& texture_binding : texture_bindings) {
          DrawTextureInfo(texture_binding);
        }
      } else {
        ImGui::Text("No vertex shader samplers");
      }
    } else {
      ImGui::TextColored(kColorError, "ERROR: no vertex shader set");
    }
  }
  if (ImGui::CollapsingHeader("Pixel Textures")) {
    auto shader = command_processor->active_pixel_shader();
    if (shader) {
      const auto& texture_bindings = shader->texture_bindings();
      if (!texture_bindings.empty()) {
        for (const auto& texture_binding : texture_bindings) {
          DrawTextureInfo(texture_binding);
        }
      } else {
        ImGui::Text("No pixel shader samplers");
      }
    } else {
      ImGui::TextColored(kColorError, "ERROR: no pixel shader set");
    }
  }
  if (ImGui::CollapsingHeader("Fetch Constants (raw)")) {
    ImGui::Columns(2);
    for (int i = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
         i <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5; ++i) {
      ImGui::Text("f%02d_%d", (i - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6,
                  (i - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) % 6);
      ImGui::NextColumn();
      ImGui::Text("%.8X", regs[i].u32);
      ImGui::NextColumn();
    }
    ImGui::Columns(1);
  }
  if (ImGui::CollapsingHeader("ALU Constants")) {
    ImGui::Columns(2);
    for (int i = XE_GPU_REG_SHADER_CONSTANT_000_X;
         i <= XE_GPU_REG_SHADER_CONSTANT_511_X; i += 4) {
      ImGui::Text("c%d", (i - XE_GPU_REG_SHADER_CONSTANT_000_X) / 4);
      ImGui::NextColumn();
      ImGui::Text("%f, %f, %f, %f", regs[i + 0].f32, regs[i + 1].f32,
                  regs[i + 2].f32, regs[i + 3].f32);
      ImGui::NextColumn();
    }
    ImGui::Columns(1);
  }
  if (ImGui::CollapsingHeader("Bool Constants")) {
    ImGui::Columns(2);
    for (int i = XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031;
         i <= XE_GPU_REG_SHADER_CONSTANT_BOOL_224_255; ++i) {
      ImGui::Text("b%03d-%03d",
                  (i - XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031) * 32,
                  (i - XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031) * 32 + 31);
      ImGui::NextColumn();
      ImGui::Text("%.8X", regs[i].u32);
      ImGui::NextColumn();
    }
    ImGui::Columns(1);
  }
  if (ImGui::CollapsingHeader("Loop Constants")) {
    ImGui::Columns(2);
    for (int i = XE_GPU_REG_SHADER_CONSTANT_LOOP_00;
         i <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31; ++i) {
      ImGui::Text("l%d", i - XE_GPU_REG_SHADER_CONSTANT_LOOP_00);
      ImGui::NextColumn();
      ImGui::Text("%.8X", regs[i].u32);
      ImGui::NextColumn();
    }
    ImGui::Columns(1);
  }
  ImGui::End();
}

}  //  namespace gpu
}  //  namespace xe
