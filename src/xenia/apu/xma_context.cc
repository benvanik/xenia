/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2021 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/apu/xma_context.h"

#include <algorithm>
#include <cstring>

#include "xenia/apu/xma_decoder.h"
#include "xenia/apu/xma_helpers.h"
#include "xenia/base/bit_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"

extern "C" {
#if XE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "third_party/FFmpeg/libavcodec/avcodec.h"
#if XE_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

// Credits for most of this code goes to:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c

namespace xe {
namespace apu {

XmaContext::XmaContext() = default;

XmaContext::~XmaContext() {
  if (av_context_) {
    if (avcodec_is_open(av_context_)) {
      avcodec_close(av_context_);
    }
    av_free(av_context_);
  }
  if (av_frame_) {
    av_frame_free(&av_frame_);
  }
  // if (current_frame_) {
  //   delete[] current_frame_;
  //  }
}

int XmaContext::Setup(uint32_t id, Memory* memory, uint32_t guest_ptr) {
  id_ = id;
  memory_ = memory;
  guest_ptr_ = guest_ptr;

  // Allocate ffmpeg stuff:
  av_packet_ = av_packet_alloc();
  assert_not_null(av_packet_);

  // find the XMA2 audio decoder
  av_codec_ = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
  if (!av_codec_) {
    XELOGE("XmaContext {}: Codec not found", id);
    return 1;
  }

  av_context_ = avcodec_alloc_context3(av_codec_);
  if (!av_context_) {
    XELOGE("XmaContext {}: Couldn't allocate context", id);
    return 1;
  }

  // Initialize these to 0. They'll actually be set later.
  av_context_->channels = 0;
  av_context_->sample_rate = 0;

  av_frame_ = av_frame_alloc();
  if (!av_frame_) {
    XELOGE("XmaContext {}: Couldn't allocate frame", id);
    return 1;
  }

  // FYI: We're purposely not opening the codec here. That is done later.
  return 0;
}

bool XmaContext::Work() {
  std::lock_guard<std::mutex> lock(lock_);
  if (!is_allocated() || !is_enabled()) {
    return false;
  }

  set_is_enabled(false);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  Decode(&data);
  data.Store(context_ptr);
  return true;
}

void XmaContext::Enable() {
  std::lock_guard<std::mutex> lock(lock_);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  XELOGAPU("XmaContext: kicking context {} (buffer {} {}/{} bits)", id(),
           data.current_buffer, data.input_buffer_read_offset,
           (data.current_buffer == 0 ? data.input_buffer_0_packet_count
                                     : data.input_buffer_1_packet_count) *
               kBitsPerPacket);

  data.Store(context_ptr);

  set_is_enabled(true);
}

bool XmaContext::Block(bool poll) {
  if (!lock_.try_lock()) {
    if (poll) {
      return false;
    }
    lock_.lock();
  }
  lock_.unlock();
  return true;
}

void XmaContext::Clear() {
  std::lock_guard<std::mutex> lock(lock_);
  XELOGAPU("XmaContext: reset context {}", id());

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  data.input_buffer_0_valid = 0;
  data.input_buffer_1_valid = 0;
  data.output_buffer_valid = 0;

  data.output_buffer_read_offset = 0;
  data.output_buffer_write_offset = 0;

  data.Store(context_ptr);
}

void XmaContext::Disable() {
  std::lock_guard<std::mutex> lock(lock_);
  XELOGAPU("XmaContext: disabling context {}", id());
  set_is_enabled(false);
}

void XmaContext::Release() {
  // Lock it in case the decoder thread is working on it now.
  std::lock_guard<std::mutex> lock(lock_);
  assert_true(is_allocated_ == true);

  set_is_allocated(false);
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));  // Zero it.
}

void XmaContext::SwapInputBuffer(XMA_CONTEXT_DATA* data) {
  // No more frames.
  if (data->current_buffer == 0) {
    data->input_buffer_0_valid = 0;
  } else {
    data->input_buffer_1_valid = 0;
  }
  data->current_buffer ^= 1;
  data->input_buffer_read_offset = 0;
}

bool XmaContext::TrySetupNextLoop(XMA_CONTEXT_DATA* data,
                                  bool ignore_input_buffer_offset) {
  // Setup the input buffer offset if next loop exists.
  // TODO(Pseudo-Kernel): Need to handle loop in the following cases.
  // 1. loop_start == loop_end == 0
  // 2. loop_start > loop_end && loop_count > 0
  if (data->loop_count > 0 && data->loop_start < data->loop_end &&
      (ignore_input_buffer_offset ||
       data->input_buffer_read_offset >= data->loop_end)) {
    // Loop back to the beginning.
    data->input_buffer_read_offset = data->loop_start;
    if (data->loop_count < 255) {
      data->loop_count--;
    }
    return true;
  }
  return false;
}

/*
void XmaContext::NextPacket(
    uint8_t* input_buffer,
    uint32_t input_size,
    uint32_t input_buffer_read_offset) {
*/
void XmaContext::NextPacket(XMA_CONTEXT_DATA* data) {
  // auto packet_idx = GetFramePacketNumber(input_buffer, input_size,
  // input_buffer_read_offset);

  // packet_idx++;
  // if (packet_idx++ >= input_size)
}

int XmaContext::GetSampleRate(int id) {
  switch (id) {
    case 0:
      return 24000;
    case 1:
      return 32000;
    case 2:
      return 44100;
    case 3:
      return 48000;
  }
  assert_always();
  return 0;
}

bool XmaContext::ValidFrameOffset(uint8_t* block, size_t size_bytes,
                                  size_t frame_offset_bits) {
  uint32_t packet_num =
      GetFramePacketNumber(block, size_bytes, frame_offset_bits);
  if (packet_num == -1) {
    // Invalid packet number
    return false;
  }

  uint8_t* packet = block + (packet_num * kBytesPerPacket);
  size_t relative_offset_bits = frame_offset_bits % kBitsPerPacket;

  uint32_t first_frame_offset = xma::GetPacketFrameOffset(packet);
  if (first_frame_offset == -1 || first_frame_offset > kBitsPerPacket) {
    // Packet only contains a partial frame, so no frames can start here.
    return false;
  }

  BitStream stream(packet, kBitsPerPacket);
  stream.SetOffset(first_frame_offset);
  while (true) {
    if (stream.offset_bits() == relative_offset_bits) {
      return true;
    }

    if (stream.BitsRemaining() < 15) {
      // Not enough room for another frame header.
      return false;
    }

    uint64_t size = stream.Read(15);
    if ((size - 15) > stream.BitsRemaining()) {
      // Last frame.
      return false;
    } else if (size == 0x7FFF) {
      // Invalid frame (and last of this packet)
      return false;
    }

    stream.Advance(size - 16);

    // Read the trailing bit to see if frames follow
    if (stream.Read(1) == 0) {
      break;
    }
  }

  return false;
}

static void dump_raw(AVFrame* frame, int id) {
  FILE* outfile = fopen(fmt::format("out{}.raw", id).c_str(), "ab");
  if (!outfile) {
    return;
  }
  size_t data_size = sizeof(float);
  for (int i = 0; i < frame->nb_samples; i++) {
    for (int ch = 0; ch < frame->channels; ch++) {
      fwrite(frame->data[ch] + data_size * i, 1, data_size, outfile);
    }
  }
  fclose(outfile);
}

void XmaContext::Decode(XMA_CONTEXT_DATA* data) {
  SCOPE_profile_cpu_f("apu");

  // What I see:
  // XMA outputs 2 bytes per sample
  // 512 samples per frame (128 per subframe)
  // Max output size is data.output_buffer_block_count * 256

  // This decoder is fed packets (max 4095 per buffer)
  // Packets contain "some" frames
  // 32bit header (big endian)

  // Frames are the smallest thing the SPUs can decode.
  // They can and usually will span packets.

  // Sample rates (data.sample_rate):
  // 0 - 24 kHz
  // 1 - 32 kHz
  // 2 - 44.1 kHz
  // 3 - 48 kHz

  // SPUs also support stereo decoding. (data.is_stereo)

  // Check the output buffer - we cannot decode anything else if it's
  // unavailable.
  if (!data->output_buffer_valid) {
    return;
  }

  // No available data.
  if (!data->input_buffer_0_valid && !data->input_buffer_1_valid) {
    data->output_buffer_valid = 0;
    return;
  }

  // XAudio Loops
  // loop_count:
  //  - XAUDIO2_MAX_LOOP_COUNT = 254
  //  - XAUDIO2_LOOP_INFINITE = 255
  // loop_start/loop_end are bit offsets to a specific frame

  // Translate pointers for future use.
  // Sometimes the game will use rolling input buffers. If they do, we cannot
  // assume they form a complete block! In addition, the buffers DO NOT have
  // to be contiguous!
  uint8_t* in0 = data->input_buffer_0_valid
                     ? memory()->TranslatePhysical(data->input_buffer_0_ptr)
                     : nullptr;
  uint8_t* in1 = data->input_buffer_1_valid
                     ? memory()->TranslatePhysical(data->input_buffer_1_ptr)
                     : nullptr;
  uint8_t* current_input_buffer = data->current_buffer ? in1 : in0;

  XELOGAPU("Processing context {} (offset {}, buffer {}, ptr {:p})", id(),
           data->input_buffer_read_offset, data->current_buffer,
           current_input_buffer);

  size_t input_buffer_0_size =
      data->input_buffer_0_packet_count * kBytesPerPacket;
  size_t input_buffer_1_size =
      data->input_buffer_1_packet_count * kBytesPerPacket;
  size_t input_total_size = input_buffer_0_size + input_buffer_1_size;

  size_t current_input_size =
      data->current_buffer ? input_buffer_1_size : input_buffer_0_size;
  size_t current_input_packet_count = current_input_size / kBytesPerPacket;

  // Output buffers are in raw PCM samples, 256 bytes per block.
  // Output buffer is a ring buffer. We need to write from the write offset
  // to the read offset.
  uint8_t* output_buffer = memory()->TranslatePhysical(data->output_buffer_ptr);
  uint32_t output_capacity =
      data->output_buffer_block_count * kBytesPerSubframeChannel;
  uint32_t output_read_offset =
      data->output_buffer_read_offset * kBytesPerSubframeChannel;
  uint32_t output_write_offset =
      data->output_buffer_write_offset * kBytesPerSubframeChannel;

  RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);

  // We can only decode an entire frame and write it out at a time, so
  // don't save any samples.
  // TODO(JoelLinn): subframes when looping
  size_t output_remaining_bytes = output_rb.write_count();
  output_remaining_bytes -=
      output_remaining_bytes % (kBytesPerFrameChannel << data->is_stereo);

  // is_dirty_ = true; // TODO
  // is_dirty_ = false;  // TODO
  assert_false(data->stop_when_done);
  assert_false(data->interrupt_when_done);
  static int total_samples = 0;
  bool reuse_input_buffer = false;
  // Decode until we can't write any more data.
  while (output_remaining_bytes > 0) {
    if (!data->input_buffer_0_valid && !data->input_buffer_1_valid) {
      // Out of data.
      break;
    }

    // Setup the input buffer if we are at loop_end.
    // The input buffer must not be swapped out until all loops are processed.
    reuse_input_buffer = TrySetupNextLoop(data, false);

    // assert_true(packets_skip_ == 0);
    // assert_true(split_frame_len_ == 0);
    // assert_true(split_frame_len_partial_ == 0);

    // Where are we in the buffer (in XMA jargon)
    int packet_idx, frame_idx, frame_count;
    uint8_t* packet;
    bool frame_last_split;

    BitStream stream(current_input_buffer, current_input_size * 8);
    stream.SetOffset(data->input_buffer_read_offset);

    // if we had a buffer swap try to skip packets first
    if (packets_skip_ > 0) {
      packet_idx =
          GetFramePacketNumber(current_input_buffer, current_input_size,
                               data->input_buffer_read_offset);
      while (packets_skip_ > 0) {
        packets_skip_--;
        packet_idx++;
        if (packet_idx >= current_input_packet_count) {
          if (!reuse_input_buffer) {
            // Last packet. Try setup once more.
            reuse_input_buffer = TrySetupNextLoop(data, true);
          }
          if (!reuse_input_buffer) {
            SwapInputBuffer(data);
          }
          return;
        }
      }
      // invalid frame pointer but needed for us
      data->input_buffer_read_offset = packet_idx * kBitsPerPacket;
      // continue;
    }

    if (split_frame_len_) {
      // handle a frame that was split over two packages
      packet_idx =
          GetFramePacketNumber(current_input_buffer, current_input_size,
                               data->input_buffer_read_offset);
      packet = current_input_buffer + packet_idx * kBytesPerPacket;
      std::tie(frame_count, frame_last_split) = GetPacketFrameCount(packet);
      frame_idx = -1;

      stream =
          BitStream(current_input_buffer, (packet_idx + 1) * kBitsPerPacket);
      stream.SetOffset(packet_idx * kBitsPerPacket + 32);

      if (split_frame_len_ > xma::kMaxFrameLength) {
        // TODO write CopyPeekMethod
        auto offset = stream.offset_bits();
        stream.Copy(
            xma_frame_.data() + 1 +
                ((split_frame_len_partial_ + split_frame_padding_start_) / 8),
            15 - split_frame_len_partial_);
        stream.SetOffset(offset);
        BitStream slen(xma_frame_.data() + 1, 15 + split_frame_padding_start_);
        slen.Advance(split_frame_padding_start_);
        split_frame_len_ = static_cast<int>(slen.Read(15));
      }

      if (frame_count > 0) {
        assert_true(xma::GetPacketFrameOffset(packet) - 32 ==
                    split_frame_len_ - split_frame_len_partial_);
      }

      auto offset = stream.Copy(
          xma_frame_.data() + 1 +
              ((split_frame_len_partial_ + split_frame_padding_start_) / 8),
          split_frame_len_ - split_frame_len_partial_);
      assert_true(offset ==
                  (split_frame_padding_start_ + split_frame_len_partial_) % 8);
    } else {
      if (data->input_buffer_read_offset % kBitsPerPacket == 0) {
        // Invalid offset. Go ahead and set it.
        int packet_number =
            GetFramePacketNumber(current_input_buffer, current_input_size,
                                 data->input_buffer_read_offset);

        if (packet_number == -1) {
          return;
        }

        auto offset =
            xma::GetPacketFrameOffset(current_input_buffer +
                                      kBytesPerPacket * packet_number) +
            data->input_buffer_read_offset;
        if (offset == -1) {
          // No more frames.
          SwapInputBuffer(data);
          // TODO partial frames? end?
          XELOGE("XmaContext {}: TODO partial frames? end?", id());
          assert_always("TODO");
          return;
        } else {
          data->input_buffer_read_offset = offset;
        }
      }

      if (!ValidFrameOffset(current_input_buffer, current_input_size,
                            data->input_buffer_read_offset)) {
        XELOGAPU("XmaContext {}: Invalid read offset {}!", id(),
                 data->input_buffer_read_offset);
        SwapInputBuffer(data);
        return;
      }

      // Where are we in the buffer (in XMA jargon)
      std::tie(packet_idx, frame_idx) =
          GetFrameNumber(current_input_buffer, current_input_size,
                         data->input_buffer_read_offset);
      // TODO handle
      assert_true(packet_idx >= 0);
      assert_true(frame_idx >= 0);
      packet = current_input_buffer + packet_idx * kBytesPerPacket;
      // frames that belong to this packet
      std::tie(frame_count, frame_last_split) = GetPacketFrameCount(packet);
      assert_true(frame_count >= 0);  // TODO end

      PrepareDecoder(packet, data->sample_rate, bool(data->is_stereo));

      // Current frame is split to next packet:
      bool frame_is_split = frame_last_split && (frame_idx >= frame_count - 1);

      stream =
          BitStream(current_input_buffer, (packet_idx + 1) * kBitsPerPacket);
      stream.SetOffset(data->input_buffer_read_offset);
      // int frame_len;
      // int frame_len_partial
      split_frame_len_partial_ = static_cast<int>(stream.BitsRemaining());
      if (split_frame_len_partial_ >= 15) {
        split_frame_len_ = static_cast<int>(stream.Peek(15));
      } else {
        // assert_always();
        split_frame_len_ = xma::kMaxFrameLength + 1;
      }
      assert_true(frame_is_split ==
                  (split_frame_len_ > split_frame_len_partial_));

      // TODO fix bitstream copy
      std::memset(xma_frame_.data(), 0, xma_frame_.size());

      {
        auto offset =
            stream.Copy(xma_frame_.data() + 1,
                        std::min(split_frame_len_, split_frame_len_partial_));
        assert_true(offset < 8);
        split_frame_padding_start_ = static_cast<uint8_t>(offset);
      }

      if (frame_is_split) {
        // go to next xma packet of this stream
        packets_skip_ = xma::GetPacketSkipCount(packet) + 1;
        while (packets_skip_ > 0) {
          packets_skip_--;
          packet += kBytesPerPacket;
          packet_idx++;
          if (packet_idx >= current_input_packet_count) {
            if (!reuse_input_buffer) {
              // Last packet. Try setup once more.
              reuse_input_buffer = TrySetupNextLoop(data, true);
            }
            if (!reuse_input_buffer) {
              SwapInputBuffer(data);
            }
            return;
          }
        }
        // TODO guest might read this:
        data->input_buffer_read_offset = packet_idx * kBitsPerPacket;
        continue;
      }
    }

    av_packet_->data = xma_frame_.data();
    av_packet_->size = static_cast<int>(
        1 + ((split_frame_padding_start_ + split_frame_len_) / 8) +
        (((split_frame_padding_start_ + split_frame_len_) % 8) ? 1 : 0));

    auto padding_end = av_packet_->size * 8 -
                       (8 + split_frame_padding_start_ + split_frame_len_);
    assert_true(padding_end < 8);
    xma_frame_[0] =
        ((split_frame_padding_start_ & 7) << 5) | ((padding_end & 7) << 2);

    split_frame_len_ = 0;
    split_frame_len_partial_ = 0;
    split_frame_padding_start_ = 0;

    auto ret = avcodec_send_packet(av_context_, av_packet_);
    if (ret < 0) {
      XELOGE("XmaContext {}: Error sending packet for decoding", id());
      // TODO bail out
      assert_always();
    }
    ret = avcodec_receive_frame(av_context_, av_frame_);
    /*
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      // TODO AVERROR_EOF???
      break;
    else
    */
    if (ret < 0) {
      XELOGE("XmaContext {}: Error during decoding", id());
      assert_always();
      return;  // TODO bail out
    }
    assert_true(ret == 0);

    {
      // copy over 1 frame
      // update input buffer read offset

      // assert(decoded_consumed_samples_ + kSamplesPerFrame <=
      //       current_frame_.size());
      assert_true(av_context_->sample_fmt == AV_SAMPLE_FMT_FLTP);
      // assert_true(frame_is_split == (frame_idx == -1));

      //			dump_raw(av_frame_, id());
      ConvertFrame((const uint8_t**)av_frame_->data, bool(data->is_stereo),
                   raw_frame_.data());
      // decoded_consumed_samples_ += kSamplesPerFrame;

      auto byte_count = kBytesPerFrameChannel << data->is_stereo;
      assert_true(output_remaining_bytes >= byte_count);
      output_rb.Write(raw_frame_.data(), byte_count);
      output_remaining_bytes -= byte_count;
      data->output_buffer_write_offset = output_rb.write_offset() / 256;

      total_samples += id_ == 0 ? kSamplesPerFrame : 0;

      uint32_t offset = data->input_buffer_read_offset;
      // if (offset % (kBytesPerSample * 8) == 0) {
      //  offset = xma::GetPacketFrameOffset(packet);
      //}
      offset = static_cast<uint32_t>(
          GetNextFrame(current_input_buffer, current_input_size, offset));
      // assert_true((offset == 0) ==
      //            (frame_is_split || (frame_idx + 1 >= frame_count)));
      if (frame_idx + 1 >= frame_count) {
        // Skip to next packet (no split frame)
        packets_skip_ = xma::GetPacketSkipCount(packet) + 1;
        while (packets_skip_ > 0) {
          packets_skip_--;
          packet_idx++;
          if (packet_idx >= current_input_packet_count) {
            if (!reuse_input_buffer) {
              // Last packet. Try setup once more.
              reuse_input_buffer = TrySetupNextLoop(data, true);
            }
            if (!reuse_input_buffer) {
              SwapInputBuffer(data);
            }
            return;
          }
        }
        packet = current_input_buffer + packet_idx * kBytesPerPacket;
        offset =
            xma::GetPacketFrameOffset(packet) + packet_idx * kBitsPerPacket;
      }
      if (offset == 0 || frame_idx == -1) {
        // Next packet but we already skipped to it
        if (packet_idx >= current_input_packet_count) {
          // Buffer is fully used
          if (!reuse_input_buffer) {
            // Last packet. Try setup once more.
            reuse_input_buffer = TrySetupNextLoop(data, true);
          }
          if (!reuse_input_buffer) {
            SwapInputBuffer(data);
          }
          break;
        }
        offset =
            xma::GetPacketFrameOffset(packet) + packet_idx * kBitsPerPacket;
      }
      // TODO buffer bounds check
      assert_true(data->input_buffer_read_offset < offset);
      data->input_buffer_read_offset = offset;
    }
  }

  // assert_true((split_frame_len_ != 0) == (data->input_buffer_read_offset ==
  // 0));

  // The game will kick us again with a new output buffer later.
  // It's important that we only invalidate this if we actually wrote to it!!
  if (output_rb.write_offset() == output_rb.read_offset()) {
    data->output_buffer_valid = 0;
  }
}

size_t XmaContext::GetNextFrame(uint8_t* block, size_t size,
                                size_t bit_offset) {
  // offset = xma::GetPacketFrameOffset(packet);
  // TODO meh
  // auto next_packet = bit_offset - bit_offset % kBitsPerPacket +
  // kBitsPerPacket;
  auto packet_idx = GetFramePacketNumber(block, size, bit_offset);

  BitStream stream(block, size * 8);
  stream.SetOffset(bit_offset);

  if (stream.BitsRemaining() < 15) {
    return 0;
  }

  uint64_t len = stream.Read(15);
  if ((len - 15) > stream.BitsRemaining()) {
    assert_always("TODO");
    // *bit_offset = next_packet;
    // return false;
    // return next_packet;
    return 0;
  } else if (len >= xma::kMaxFrameLength) {
    assert_always("TODO");
    // *bit_offset = next_packet;
    // return false;
    return 0;
    // return next_packet;
  }

  stream.Advance(len - (15 + 1));
  // Read the trailing bit to see if frames follow
  if (stream.Read(1) == 0) {
    return 0;
  }

  bit_offset += len;
  if (packet_idx < GetFramePacketNumber(block, size, bit_offset)) {
    return 0;
  }
  return bit_offset;
}

int XmaContext::GetFramePacketNumber(uint8_t* block, size_t size,
                                     size_t bit_offset) {
  size *= 8;
  if (bit_offset >= size) {
    // Not good :(
    assert_always();
    return -1;
  }

  size_t byte_offset = bit_offset >> 3;
  size_t packet_number = byte_offset / kBytesPerPacket;

  return (uint32_t)packet_number;
}

std::tuple<int, int> XmaContext::GetFrameNumber(uint8_t* block, size_t size,
                                                size_t bit_offset) {
  auto packet_idx = GetFramePacketNumber(block, size, bit_offset);

  if (packet_idx < 0 || (packet_idx + 1) * kBytesPerPacket > size) {
    assert_always();
    return {packet_idx, -2};
  }

  if (bit_offset == 0) {
    return {packet_idx, -1};
  }

  uint8_t* packet = block + (packet_idx * kBytesPerPacket);
  auto first_frame_offset = xma::GetPacketFrameOffset(packet);
  BitStream stream(block, size * 8);
  stream.SetOffset(packet_idx * kBitsPerPacket + first_frame_offset);

  int frame_idx = 0;
  while (true) {
    if (stream.BitsRemaining() < 15) {
      break;
    }

    if (stream.offset_bits() == bit_offset) {
      break;
    }

    uint64_t size = stream.Read(15);
    if ((size - 15) > stream.BitsRemaining()) {
      // Last frame.
      break;
    } else if (size == 0x7FFF) {
      // Invalid frame (and last of this packet)
      break;
    }

    stream.Advance(size - (15 + 1));

    // Read the trailing bit to see if frames follow
    if (stream.Read(1) == 0) {
      break;
    }
    frame_idx++;
  }
  return {packet_idx, frame_idx};
}

std::tuple<int, bool> XmaContext::GetPacketFrameCount(uint8_t* packet) {
  auto first_frame_offset = xma::GetPacketFrameOffset(packet);
  if (first_frame_offset > kBitsPerPacket - kBitsPerHeader) {
    // frame offset is beyond packet end
    return {0, false};
  }

  BitStream stream(packet, kBitsPerPacket);
  stream.SetOffset(first_frame_offset);
  int frame_count = 0;

  while (true) {
    frame_count++;
    if (stream.BitsRemaining() < 15) {
      return {frame_count, true};
    }

    uint64_t size = stream.Read(15);
    if ((size - 15) > stream.BitsRemaining()) {
      return {frame_count, true};
    } else if (size == 0x7FFF) {
      assert_always();
      return {frame_count, true};
    }

    stream.Advance(size - (15 + 1));

    if (stream.Read(1) == 0) {
      return {frame_count, false};
    }
  }
}

int XmaContext::PrepareDecoder(uint8_t* packet, int sample_rate,
                               bool is_two_channel) {
  // Sanity check: Packet metadata is always 1 for XMA2/0 for XMA
  assert_true((packet[2] & 0x7) == 1 || (packet[2] & 0x7) == 0);

  sample_rate = GetSampleRate(sample_rate);

  // Re-initialize the context with new sample rate and channels.
  uint32_t channels = is_two_channel ? 2 : 1;
  if (av_context_->sample_rate != sample_rate ||
      av_context_->channels != channels) {
    // We have to reopen the codec so it'll realloc whatever data it needs.
    // TODO(DrChat): Find a better way.
    avcodec_close(av_context_);

    av_context_->sample_rate = sample_rate;
    av_context_->channels = channels;

    if (avcodec_open2(av_context_, av_codec_, NULL) < 0) {
      XELOGE("XmaContext: Failed to reopen FFmpeg context");
      return -1;
    }
    return 1;
  }
  return 0;
}

void XmaContext::ConvertFrame(const uint8_t** samples, bool is_two_channel,
                              uint8_t* output_buffer) {
  // Loop through every sample, convert and drop it into the output array.
  // If more than one channel, we need to interleave the samples from each
  // channel next to each other. Always saturate because FFmpeg output is
  // not limited to [-1, 1] (for example 1.095 as seen in RDR)
  constexpr float scale = (1 << 15) - 1;
  auto out = reinterpret_cast<int16_t*>(output_buffer);

  // For testing of vectorized versions, stereo audio is common in Halo 3, since
  // the first menu frame; the intro cutscene also has more than 2 channels.
#if XE_ARCH_AMD64
  static_assert(kSamplesPerFrame % 8 == 0);
  const auto in_channel_0 = reinterpret_cast<const float*>(samples[0]);
  const __m128 scale_mm = _mm_set1_ps(scale);
  if (is_two_channel) {
    const auto in_channel_1 = reinterpret_cast<const float*>(samples[1]);
    const __m128i shufmask =
        _mm_set_epi8(14, 15, 6, 7, 12, 13, 4, 5, 10, 11, 2, 3, 8, 9, 0, 1);
    for (uint32_t i = 0; i < kSamplesPerFrame; i += 4) {
      // Load 8 samples, 4 for each channel.
      __m128 in_mm0 = _mm_loadu_ps(&in_channel_0[i]);
      __m128 in_mm1 = _mm_loadu_ps(&in_channel_1[i]);
      // Rescale.
      in_mm0 = _mm_mul_ps(in_mm0, scale_mm);
      in_mm1 = _mm_mul_ps(in_mm1, scale_mm);
      // Cast to int32.
      __m128i out_mm0 = _mm_cvtps_epi32(in_mm0);
      __m128i out_mm1 = _mm_cvtps_epi32(in_mm1);
      // Saturated cast and pack to int16.
      __m128i out_mm = _mm_packs_epi32(out_mm0, out_mm1);
      // Interleave channels and byte swap.
      out_mm = _mm_shuffle_epi8(out_mm, shufmask);
      // Store, as [out + i * 4] movdqu.
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i * 2]), out_mm);
    }
  } else {
    const __m128i shufmask =
        _mm_set_epi8(14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
    for (uint32_t i = 0; i < kSamplesPerFrame; i += 8) {
      // Load 8 samples, as [in_channel_0 + i * 4] and
      // [in_channel_0 + i * 4 + 16] movups.
      __m128 in_mm0 = _mm_loadu_ps(&in_channel_0[i]);
      __m128 in_mm1 = _mm_loadu_ps(&in_channel_0[i + 4]);
      // Rescale.
      in_mm0 = _mm_mul_ps(in_mm0, scale_mm);
      in_mm1 = _mm_mul_ps(in_mm1, scale_mm);
      // Cast to int32.
      __m128i out_mm0 = _mm_cvtps_epi32(in_mm0);
      __m128i out_mm1 = _mm_cvtps_epi32(in_mm1);
      // Saturated cast and pack to int16.
      __m128i out_mm = _mm_packs_epi32(out_mm0, out_mm1);
      // Byte swap.
      out_mm = _mm_shuffle_epi8(out_mm, shufmask);
      // Store, as [out + i * 2] movdqu.
      _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i]), out_mm);
    }
  }
#else
  uint32_t o = 0;
  for (uint32_t i = 0; i < kSamplesPerFrame; i++) {
    for (uint32_t j = 0; j <= uint32_t(is_two_channel); j++) {
      // Select the appropriate array based on the current channel.
      auto in = reinterpret_cast<const float*>(samples[j]);

      // Raw samples sometimes aren't within [-1, 1]
      float scaled_sample = xe::saturate_signed(in[i]) * scale;

      // Convert the sample and output it in big endian.
      auto sample = static_cast<int16_t>(scaled_sample);
      out[o++] = xe::byte_swap(sample);
    }
  }
#endif
}

}  // namespace apu
}  // namespace xe
