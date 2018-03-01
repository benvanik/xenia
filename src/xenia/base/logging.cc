/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"

#include <gflags/gflags.h>

#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "xenia/base/atomic.h"
#include "xenia/base/debugging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/threading.h"

// For MessageBox:
// TODO(benvanik): generic API? logging_win.cc?
// TODO(dougvj): For now I just inlined a small difference for linux
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_string(log_file, "",
              "Logs are written to the given file (specify stdout/stderr for "
              "command line)");
DEFINE_bool(log_debugprint, false, "Dump the log to DebugPrint.");
DEFINE_bool(flush_log, true, "Flush log file after each log line batch.");
DEFINE_int32(
    log_level, 2,
    "Maximum level to be logged. (0=error, 1=warning, 2=info, 3=debug)");

namespace xe {

class Logger;

Logger* logger_ = nullptr;
thread_local std::vector<char> log_format_buffer_(64 * 1024);

class Logger {
 public:
  explicit Logger(const std::wstring& app_name) : running_(true) {
    if (FLAGS_log_file.empty()) {
      // Default to app name.
      auto file_path = app_name + L".log";
      xe::filesystem::CreateParentFolder(file_path);
      file_ = xe::filesystem::OpenFile(file_path, "wt");
    } else {
      if (FLAGS_log_file == "stdout") {
        file_ = stdout;
      } else if (FLAGS_log_file == "stderr") {
        file_ = stderr;
      } else {
        auto file_path = xe::to_wstring(FLAGS_log_file.c_str());
        xe::filesystem::CreateParentFolder(file_path);
        file_ = xe::filesystem::OpenFile(file_path, "wt");
      }
    }

    write_thread_ =
        xe::threading::Thread::Create({}, [this]() { WriteThread(); });
    write_thread_->set_name("xe::FileLogSink Writer");
  }

  ~Logger() {
    running_ = false;
    xe::threading::Wait(write_thread_.get(), true);
    fflush(file_);
    fclose(file_);
  }

  void AppendLine(uint32_t thread_id, LogLevel level, const char prefix_char,
                  const char* buffer, size_t buffer_length) {
    if (static_cast<int32_t>(level) > FLAGS_log_level) {
      // Discard this line.
      return;
    }

    LogLine line;
    line.buffer_length = buffer_length;
    line.thread_id = thread_id;
    line.prefix_char = prefix_char;

    // First, run a check and see if we can increment write
    // head without any problems. If so, cmpxchg it to reserve some space in the
    // ring. If someone beats us, loop around.
    //
    // Once we have a reservation, write our data and then increment the write
    // tail.
    size_t size = sizeof(LogLine) + buffer_length;
    while (true) {
      // Attempt to make a reservation.
      size_t write_head = write_head_;
      size_t read_head = read_head_;

      RingBuffer rb(buffer_, kBufferSize);
      rb.set_write_offset(write_head);
      rb.set_read_offset(read_head);
      if (rb.write_count() < size) {
        xe::threading::MaybeYield();
        continue;
      }

      // We have enough size to make a reservation!
      rb.AdvanceWrite(size);
      if (xe::atomic_cas(write_head, rb.write_offset(), &write_head_)) {
        // Reservation made. Write out logline.
        rb.set_write_offset(write_head);
        rb.Write(&line, sizeof(LogLine));
        rb.Write(buffer, buffer_length);

        while (!xe::atomic_cas(write_head, rb.write_offset(), &write_tail_)) {
          // Done writing. End the reservation.
          xe::threading::MaybeYield();
        }

        break;
      } else {
        // Someone beat us to the chase. Loop around.
        continue;
      }
    }
  }

 private:
  static const size_t kBufferSize = 8 * 1024 * 1024;

  struct LogLine {
    size_t buffer_length;
    uint32_t thread_id;
    uint16_t _pad_0;  // (2b) padding
    uint8_t _pad_1;   // (1b) padding
    char prefix_char;
  };

  void Write(const char* buf, size_t size) {
    if (file_) {
      fwrite(buf, 1, size, file_);
    }

    if (FLAGS_log_debugprint) {
      debugging::DebugPrint("%.*s", size, buf);
    }
  }

  void WriteThread() {
    RingBuffer rb(buffer_, kBufferSize);
    uint32_t idle_loops = 0;
    while (running_) {
      bool did_write = false;
      rb.set_write_offset(write_tail_);
      while (!rb.empty()) {
        did_write = true;

        // Read line header and write out the line prefix.
        LogLine line;
        rb.Read(&line, sizeof(line));
        char prefix[] = {
            line.prefix_char,
            '>',
            ' ',
            '0',  // Thread ID gets placed here (8 chars).
            '0',
            '0',
            '0',
            '0',
            '0',
            '0',
            '0',
            ' ',
            0,
        };
        std::snprintf(prefix + 3, sizeof(prefix) - 3, "%08" PRIX32 " ",
                      line.thread_id);
        Write(prefix, sizeof(prefix) - 1);
        if (line.buffer_length) {
          // Get access to the line data - which may be split in the ring buffer
          // - and write it out in parts.
          auto line_range = rb.BeginRead(line.buffer_length);
          Write(reinterpret_cast<const char*>(line_range.first),
                line_range.first_length);
          if (line_range.second_length) {
            Write(reinterpret_cast<const char*>(line_range.second),
                  line_range.second_length);
          }
          // Always ensure there is a newline.
          char last_char = line_range.second
                               ? line_range.second[line_range.second_length - 1]
                               : line_range.first[line_range.first_length - 1];
          if (last_char != '\n') {
            const char suffix[1] = {'\n'};
            Write(suffix, 1);
          }
          rb.EndRead(std::move(line_range));
        } else {
          const char suffix[1] = {'\n'};
          Write(suffix, 1);
        }

        rb.set_write_offset(write_tail_);
        read_head_ = rb.read_offset();
      }
      if (did_write) {
        if (FLAGS_flush_log) {
          fflush(file_);
        }

        idle_loops = 0;
      } else {
        if (idle_loops > 1000) {
          // Introduce a waiting period.
          xe::threading::Sleep(std::chrono::milliseconds(50));
        }

        idle_loops++;
      }
    }
  }

  size_t write_head_ = 0;
  size_t write_tail_ = 0;
  size_t read_head_ = 0;
  uint8_t buffer_[kBufferSize];
  FILE* file_ = nullptr;

  std::atomic<bool> running_;
  std::unique_ptr<xe::threading::Thread> write_thread_;
};

void InitializeLogging(const std::wstring& app_name) {
  // We leak this intentionally - lots of cleanup code needs it.
  auto mem = memory::AlignedAlloc<Logger>(0x10);
  logger_ = new (mem) Logger(app_name);
}

void LogLineFormat(LogLevel log_level, const char prefix_char, const char* fmt,
                   ...) {
  va_list args;
  va_start(args, fmt);
  int chars_written = vsnprintf(log_format_buffer_.data(),
                                log_format_buffer_.capacity(), fmt, args);
  va_end(args);
  if (chars_written >= 0 && chars_written < log_format_buffer_.capacity()) {
    logger_->AppendLine(xe::threading::current_thread_id(), log_level,
                        prefix_char, log_format_buffer_.data(), chars_written);
  } else if (chars_written >= 0) {
    logger_->AppendLine(xe::threading::current_thread_id(), log_level,
                        prefix_char, fmt, std::strlen(fmt));
  }
}

void LogLineVarargs(LogLevel log_level, const char prefix_char, const char* fmt,
                    va_list args) {
  int chars_written = vsnprintf(log_format_buffer_.data(),
                                log_format_buffer_.capacity(), fmt, args);
  if (chars_written < 0) {
    return;
  }

  auto size =
      std::min(size_t(chars_written), log_format_buffer_.capacity() - 1);
  logger_->AppendLine(xe::threading::current_thread_id(), log_level,
                      prefix_char, log_format_buffer_.data(), size);
}

void LogLine(LogLevel log_level, const char prefix_char, const char* str,
             size_t str_length) {
  logger_->AppendLine(
      xe::threading::current_thread_id(), log_level, prefix_char, str,
      str_length == std::string::npos ? std::strlen(str) : str_length);
}

void LogLine(LogLevel log_level, const char prefix_char,
             const std::string& str) {
  logger_->AppendLine(xe::threading::current_thread_id(), log_level,
                      prefix_char, str.c_str(), str.length());
}

void FatalError(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogLineVarargs(LogLevel::LOG_LEVEL_ERROR, 'X', fmt, args);
  va_end(args);

#if XE_PLATFORM_WIN32
  if (!xe::has_console_attached()) {
    va_start(args, fmt);
    vsnprintf(log_format_buffer_.data(), log_format_buffer_.capacity(), fmt,
              args);
    va_end(args);
    MessageBoxA(NULL, log_format_buffer_.data(), "Xenia Error",
                MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
  }
#elif XE_PLATFORM_LINUX
  fprintf(stderr, fmt, args);
#endif  // WIN32

  exit(1);
}

void FatalError(const std::string& str) { FatalError(str.c_str()); }

}  // namespace xe
