/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_CONTEXT_H_
#define XENIA_APU_XMA_CONTEXT_H_

#include <array>
#include <atomic>
#include <mutex>
#include <queue>
//#include <vector>

#include "xenia/memory.h"
#include "xenia/xbox.h"

// XMA audio format:
// From research, XMA appears to be based on WMA Pro with
// a few (very slight) modifications.
// XMA2 is fully backwards-compatible with XMA1.

// Helpful resources:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c
// https://hcs64.com/mboard/forum.php?showthread=14818
// https://github.com/hrydgard/minidx9/blob/master/Include/xma2defs.h

// Forward declarations
struct AVCodec;
struct AVCodecParserContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xe {
namespace apu {

// This is stored in guest space in big-endian order.
// We load and swap the whole thing to splat here so that we can
// use bitfields.
// This could be important:
// https://www.fmod.org/questions/question/forum-15859
// Appears to be dumped in order (for the most part)

struct XMA_CONTEXT_DATA {
  // DWORD 0
  uint32_t input_buffer_0_packet_count : 12;  // XMASetInputBuffer0, number of
                                              // 2KB packets. Max 4095 packets.
                                              // These packets form a block.
  uint32_t loop_count : 8;                    // +12bit, XMASetLoopData NumLoops
  uint32_t input_buffer_0_valid : 1;          // +20bit, XMAIsInputBuffer0Valid
  uint32_t input_buffer_1_valid : 1;          // +21bit, XMAIsInputBuffer1Valid
  uint32_t output_buffer_block_count : 5;     // +22bit SizeWrite 256byte blocks
  uint32_t output_buffer_write_offset : 5;    // +27bit
                                              // XMAGetOutputBufferWriteOffset
                                              // AKA OffsetWrite

  // DWORD 1
  uint32_t input_buffer_1_packet_count : 12;  // XMASetInputBuffer1, number of
                                              // 2KB packets. Max 4095 packets.
                                              // These packets form a block.
  uint32_t loop_subframe_start : 2;           // +12bit, XMASetLoopData
  uint32_t loop_subframe_end : 3;             // +14bit, XMASetLoopData
  uint32_t loop_subframe_skip : 3;            // +17bit, XMASetLoopData might be
                                              // subframe_decode_count
  uint32_t subframe_decode_count : 4;         // +20bit
  uint32_t subframe_skip_count : 3;           // +24bit
  uint32_t sample_rate : 2;                   // +27bit enum of sample rates
  uint32_t is_stereo : 1;                     // +29bit
  uint32_t unk_dword_1_c : 1;                 // +30bit
  uint32_t output_buffer_valid : 1;           // +31bit, XMAIsOutputBufferValid

  // DWORD 2
  uint32_t input_buffer_read_offset : 26;  // XMAGetInputBufferReadOffset
  uint32_t unk_dword_2 : 6;                // ErrorStatus/ErrorSet (?)

  // DWORD 3
  uint32_t loop_start : 26;  // XMASetLoopData LoopStartOffset
                             // frame offset in bits
  uint32_t unk_dword_3 : 6;  // ? ParserErrorStatus/ParserErrorSet(?)

  // DWORD 4
  uint32_t loop_end : 26;        // XMASetLoopData LoopEndOffset
                                 // frame offset in bits
  uint32_t packet_metadata : 5;  // XMAGetPacketMetadata
  uint32_t current_buffer : 1;   // ?

  // DWORD 5
  uint32_t input_buffer_0_ptr;  // physical address
  // DWORD 6
  uint32_t input_buffer_1_ptr;  // physical address
  // DWORD 7
  uint32_t output_buffer_ptr;  // physical address
  // DWORD 8
  uint32_t work_buffer_ptr;  // PtrOverlapAdd(?)

  // DWORD 9
  // +0bit, XMAGetOutputBufferReadOffset AKA WriteBufferOffsetRead
  uint32_t output_buffer_read_offset : 5;
  uint32_t : 25;
  uint32_t stop_when_done : 1;       // +30bit
  uint32_t interrupt_when_done : 1;  // +31bit

  // DWORD 10-15
  uint32_t unk_dwords_10_15[6];  // reserved?

  explicit XMA_CONTEXT_DATA(const void* ptr) {
    xe::copy_and_swap(reinterpret_cast<uint32_t*>(this),
                      reinterpret_cast<const uint32_t*>(ptr),
                      sizeof(XMA_CONTEXT_DATA) / 4);
  }

  void Store(void* ptr) {
    xe::copy_and_swap(reinterpret_cast<uint32_t*>(ptr),
                      reinterpret_cast<const uint32_t*>(this),
                      sizeof(XMA_CONTEXT_DATA) / 4);
  }
};
static_assert_size(XMA_CONTEXT_DATA, 64);

#pragma pack(push, 1)
// XMA2WAVEFORMATEX
struct Xma2ExtraData {
  uint8_t raw[34];
};
static_assert_size(Xma2ExtraData, 34);
#pragma pack(pop)

class XmaContext {
 public:
  static const uint32_t kBytesPerPacket = 2048;
  static const uint32_t kBitsPerPacket = kBytesPerPacket * 8;
  static const uint32_t kBitsPerHeader = 33;

  static const uint32_t kBytesPerSample = 2;
  static const uint32_t kSamplesPerFrame = 512;
  static const uint32_t kSamplesPerSubframe = 128;
  static const uint32_t kBytesPerFrameChannel =
      kSamplesPerFrame * kBytesPerSample;
  static const uint32_t kBytesPerSubframeChannel =
      kSamplesPerSubframe * kBytesPerSample;

  // static const uint32_t kOutputBytesPerBlock = 256;
  // static const uint32_t kOutputMaxSizeBytes = 31 * kOutputBytesPerBlock;

  explicit XmaContext();
  ~XmaContext();

  int Setup(uint32_t id, Memory* memory, uint32_t guest_ptr);
  bool Work();

  void Enable();
  bool Block(bool poll);
  void Clear();
  void Disable();
  void Release();

  Memory* memory() const { return memory_; }

  uint32_t id() { return id_; }
  uint32_t guest_ptr() { return guest_ptr_; }
  bool is_allocated() { return is_allocated_; }
  bool is_enabled() { return is_enabled_; }

  void set_is_allocated(bool is_allocated) { is_allocated_ = is_allocated; }
  void set_is_enabled(bool is_enabled) { is_enabled_ = is_enabled; }

 private:
  static void SwapInputBuffer(XMA_CONTEXT_DATA* data);
  static bool TrySetupNextLoop(XMA_CONTEXT_DATA* data,
                               bool ignore_input_buffer_offset);
  static void NextPacket(XMA_CONTEXT_DATA* data);
  static int GetSampleRate(int id);
  // Get the offset of the next frame. Does not traverse packets.
  static size_t GetNextFrame(uint8_t* block, size_t size, size_t bit_offset);
  // Get the containing packet number of the frame pointed to by the offset.
  static int GetFramePacketNumber(uint8_t* block, size_t size,
                                  size_t bit_offset);
  // Get the packet number and the index of the frame inside that packet
  static std::tuple<int, int> GetFrameNumber(uint8_t* block, size_t size,
                                             size_t bit_offset);
  // Get the number of frames contained in the packet (including truncated) and
  // if the last frame is split.
  static std::tuple<int, bool> GetPacketFrameCount(uint8_t* packet);

  // Convert sample format and swap bytes
  static void ConvertFrame(const uint8_t** samples, bool is_two_channel,
                           uint8_t* output_buffer);

  bool ValidFrameOffset(uint8_t* block, size_t size_bytes,
                        size_t frame_offset_bits);
  void Decode(XMA_CONTEXT_DATA* data);
  int PrepareDecoder(uint8_t* packet, int sample_rate, bool is_two_channel);

  Memory* memory_ = nullptr;

  uint32_t id_ = 0;
  uint32_t guest_ptr_ = 0;
  std::mutex lock_;
  bool is_allocated_ = false;
  bool is_enabled_ = false;
  // bool is_dirty_ = true;

  // ffmpeg structures
  AVPacket* av_packet_ = nullptr;
  AVCodec* av_codec_ = nullptr;
  AVCodecContext* av_context_ = nullptr;
  AVFrame* av_frame_ = nullptr;
  // uint32_t decoded_consumed_samples_ = 0; // TODO do this dynamically
  // int decoded_idx_ = -1;

  // bool partial_frame_saved_ = false;
  // bool partial_frame_size_known_ = false;
  // size_t partial_frame_total_size_bits_ = 0;
  // size_t partial_frame_start_offset_bits_ = 0;
  // size_t partial_frame_offset_bits_ = 0;  // blah internal don't use this
  // std::vector<uint8_t> partial_frame_buffer_;
  uint32_t packets_skip_ = 0;

  // bool split_frame_pending_ = false;
  uint32_t split_frame_len_ = 0;
  uint32_t split_frame_len_partial_ = 0;
  uint8_t split_frame_padding_start_ = 0;
  // first byte contains bit offset information
  std::array<uint8_t, 1 + 4096> xma_frame_;

  // uint8_t* current_frame_ = nullptr;
  // conversion buffer for 2 channel frame
  std::array<uint8_t, kBytesPerFrameChannel * 2> raw_frame_;
  // std::vector<uint8_t> current_frame_ = std::vector<uint8_t>(0);
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_CONTEXT_H_
