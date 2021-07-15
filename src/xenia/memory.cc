/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/memory.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/mmio_handler.h"

// TODO(benvanik): move xbox.h out
#include "xenia/xbox.h"

DEFINE_bool(protect_zero, true, "Protect the zero page from reads and writes.",
            "Memory");
DEFINE_bool(protect_on_release, false,
            "Protect released memory to prevent accesses.", "Memory");
DEFINE_bool(scribble_heap, false,
            "Scribble 0xCD into all allocated heap memory.", "Memory");

namespace xe {
uint32_t get_page_count(uint32_t value, uint32_t page_size) {
  return xe::round_up(value, page_size) / page_size;
}

/**
 * Memory map:
 * 0x00000000 - 0x3FFFFFFF (1024mb) - virtual 4k pages
 * 0x40000000 - 0x7FFFFFFF (1024mb) - virtual 64k pages
 * 0x80000000 - 0x8BFFFFFF ( 192mb) - xex 64k pages
 * 0x8C000000 - 0x8FFFFFFF (  64mb) - xex 64k pages (encrypted)
 * 0x90000000 - 0x9FFFFFFF ( 256mb) - xex 4k pages
 * 0xA0000000 - 0xBFFFFFFF ( 512mb) - physical 64k pages
 * 0xC0000000 - 0xDFFFFFFF          - physical 16mb pages
 * 0xE0000000 - 0xFFFFFFFF          - physical 4k pages
 *
 * We use the host OS to create an entire addressable range for this. That way
 * we don't have to emulate a TLB. It'd be really cool to pass through page
 * sizes or use madvice to let the OS know what to expect.
 *
 * We create our own heap of committed memory that lives at
 * memory_HEAP_LOW to memory_HEAP_HIGH - all normal user allocations
 * come from there. Since the Xbox has no paging, we know that the size of
 * this heap will never need to be larger than ~512MB (realistically, smaller
 * than that). We place it far away from the XEX data and keep the memory
 * around it uncommitted so that we have some warning if things go astray.
 *
 * For XEX/GPU/etc data we allow placement allocations (base_address != 0) and
 * commit the requested memory as needed. This bypasses the standard heap, but
 * XEXs should never be overwriting anything so that's fine. We can also query
 * for previous commits and assert that we really isn't committing twice.
 *
 * GPU memory is mapped onto the lower 512mb of the virtual 4k range (0).
 * So 0xA0000000 = 0x00000000. A more sophisticated allocator could handle
 * this.
 */

static Memory* active_memory_ = nullptr;

void CrashDump() {
  static std::atomic<int> in_crash_dump(0);
  if (in_crash_dump.fetch_add(1)) {
    xe::FatalError(
        "Hard crash: the memory system crashed while dumping a crash dump.");
    return;
  }
  active_memory_->DumpMap();
  --in_crash_dump;
}

Memory::Memory() {
  system_page_size_ = uint32_t(xe::memory::page_size());
  system_allocation_granularity_ =
      uint32_t(xe::memory::allocation_granularity());
  assert_zero(active_memory_);
  active_memory_ = this;
}

Memory::~Memory() {
  assert_true(active_memory_ == this);
  active_memory_ = nullptr;

  // Uninstall the MMIO handler, as we won't be able to service more
  // requests.
  mmio_handler_.reset();

  for (auto invalidation_callback : physical_memory_invalidation_callbacks_) {
    delete invalidation_callback;
  }

  heaps_.v00000000.Dispose();
  heaps_.v40000000.Dispose();
  heaps_.v80000000.Dispose();
  heaps_.v90000000.Dispose();
  heaps_.vA0000000.Dispose();
  heaps_.vC0000000.Dispose();
  heaps_.vE0000000.Dispose();
  heaps_.physical.Dispose();

  // Unmap all views and close mapping.
  if (mapping_ != xe::memory::kFileMappingHandleInvalid) {
    UnmapViews();
    xe::memory::CloseFileMappingHandle(mapping_, file_name_);
    mapping_base_ = nullptr;
    mapping_ = xe::memory::kFileMappingHandleInvalid;
  }

  virtual_membase_ = nullptr;
  physical_membase_ = nullptr;
}

bool Memory::Initialize() {
  file_name_ = fmt::format("xenia_memory_{}", Clock::QueryHostTickCount());

  // Create main page file-backed mapping. This is all reserved but
  // uncommitted (so it shouldn't expand page file).
  mapping_ = xe::memory::CreateFileMappingHandle(
      file_name_,
      // entire 4gb space + 512mb physical:
      0x11FFFFFFF, xe::memory::PageAccess::kReadWrite, false);
  if (mapping_ == xe::memory::kFileMappingHandleInvalid) {
    XELOGE("Unable to reserve the 4gb guest address space.");
    assert_always();
    return false;
  }

  // Attempt to create our views. This may fail at the first address
  // we pick, so try a few times.
  mapping_base_ = 0;
  for (size_t n = 32; n < 64; n++) {
    auto mapping_base = reinterpret_cast<uint8_t*>(1ull << n);
    if (!MapViews(mapping_base)) {
      mapping_base_ = mapping_base;
      break;
    }
  }
  if (!mapping_base_) {
    XELOGE("Unable to find a continuous block in the 64bit address space.");
    assert_always();
    return false;
  }
  virtual_membase_ = mapping_base_;
  physical_membase_ = mapping_base_ + 0x100000000ull;

  // Prepare virtual heaps.
  heaps_.v00000000.Initialize(this, virtual_membase_, HeapType::kGuestVirtual,
                              0x00000000, 0x40000000, 4096);
  heaps_.v40000000.Initialize(this, virtual_membase_, HeapType::kGuestVirtual,
                              0x40000000, 0x40000000 - 0x01000000, 64 * 1024);
  heaps_.v80000000.Initialize(this, virtual_membase_, HeapType::kGuestXex,
                              0x80000000, 0x10000000, 64 * 1024);
  heaps_.v90000000.Initialize(this, virtual_membase_, HeapType::kGuestXex,
                              0x90000000, 0x10000000, 4096);

  // Prepare physical heaps.
  heaps_.physical.Initialize(this, physical_membase_, HeapType::kGuestPhysical,
                             0x00000000, 0x20000000, 4096);
  heaps_.vA0000000.Initialize(this, virtual_membase_, HeapType::kGuestPhysical,
                              0xA0000000, 0x20000000, 64 * 1024,
                              &heaps_.physical);
  heaps_.vC0000000.Initialize(this, virtual_membase_, HeapType::kGuestPhysical,
                              0xC0000000, 0x20000000, 16 * 1024 * 1024,
                              &heaps_.physical);
  heaps_.vE0000000.Initialize(this, virtual_membase_, HeapType::kGuestPhysical,
                              0xE0000000, 0x1FD00000, 4096, &heaps_.physical);

  // Protect the first and last 64kb of memory.
  heaps_.v00000000.AllocFixed(
      0x00000000, 0x10000, 0x10000,
      kMemoryAllocationReserve | kMemoryAllocationCommit,
      !cvars::protect_zero ? kMemoryProtectRead | kMemoryProtectWrite
                           : kMemoryProtectNoAccess);
  heaps_.physical.AllocFixed(0x1FFF0000, 0x10000, 0x10000,
                             kMemoryAllocationReserve, kMemoryProtectNoAccess);

  // GPU writeback.
  // 0xC... is physical, 0x7F... is virtual. We may need to overlay these.
  heaps_.vC0000000.AllocFixed(
      0xC0000000, 0x01000000, 32,
      kMemoryAllocationReserve | kMemoryAllocationCommit,
      kMemoryProtectRead | kMemoryProtectWrite);

  // Add handlers for MMIO.
  mmio_handler_ = cpu::MMIOHandler::Install(
      virtual_membase_, physical_membase_, physical_membase_ + 0x1FFFFFFF,
      HostToGuestVirtualThunk, this, AccessViolationCallbackThunk, this);
  if (!mmio_handler_) {
    XELOGE("Unable to install MMIO handlers");
    assert_always();
    return false;
  }

  // ?
  uint32_t unk_phys_alloc;
  heaps_.vA0000000.Alloc(0x340000, 64 * 1024, kMemoryAllocationReserve,
                         kMemoryProtectNoAccess, true, &unk_phys_alloc);

  return true;
}

static const struct {
  uint64_t virtual_address_start;
  uint64_t virtual_address_end;
  uint64_t target_address;
} map_info[] = {
    // (1024mb) - virtual 4k pages
    {
        0x00000000,
        0x3FFFFFFF,
        0x0000000000000000ull,
    },
    // (1024mb) - virtual 64k pages (cont)
    {
        0x40000000,
        0x7EFFFFFF,
        0x0000000040000000ull,
    },
    //   (16mb) - GPU writeback + 15mb of XPS?
    {
        0x7F000000,
        0x7FFFFFFF,
        0x0000000100000000ull,
    },
    //  (256mb) - xex 64k pages
    {
        0x80000000,
        0x8FFFFFFF,
        0x0000000080000000ull,
    },
    //  (256mb) - xex 4k pages
    {
        0x90000000,
        0x9FFFFFFF,
        0x0000000080000000ull,
    },
    //  (512mb) - physical 64k pages
    {
        0xA0000000,
        0xBFFFFFFF,
        0x0000000100000000ull,
    },
    //          - physical 16mb pages
    {
        0xC0000000,
        0xDFFFFFFF,
        0x0000000100000000ull,
    },
    //          - physical 4k pages
    {
        0xE0000000,
        0xFFFFFFFF,
        0x0000000100001000ull,
    },
    //          - physical raw
    {
        0x100000000,
        0x11FFFFFFF,
        0x0000000100000000ull,
    },
};
int Memory::MapViews(uint8_t* mapping_base) {
  assert_true(xe::countof(map_info) == xe::countof(views_.all_views));
  // 0xE0000000 4 KB offset is emulated via host_address_offset and on the CPU
  // side if system allocation granularity is bigger than 4 KB.
  uint64_t granularity_mask = ~uint64_t(system_allocation_granularity_ - 1);
  for (size_t n = 0; n < xe::countof(map_info); n++) {
    views_.all_views[n] = reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
        mapping_, mapping_base + map_info[n].virtual_address_start,
        map_info[n].virtual_address_end - map_info[n].virtual_address_start + 1,
        xe::memory::PageAccess::kReadWrite,
        map_info[n].target_address & granularity_mask));
    if (!views_.all_views[n]) {
      // Failed, so bail and try again.
      UnmapViews();
      return 1;
    }
  }
  return 0;
}

void Memory::UnmapViews() {
  for (size_t n = 0; n < xe::countof(views_.all_views); n++) {
    if (views_.all_views[n]) {
      size_t length = map_info[n].virtual_address_end -
                      map_info[n].virtual_address_start + 1;
      xe::memory::UnmapFileView(mapping_, views_.all_views[n], length);
    }
  }
}

void Memory::Reset() {
  heaps_.v00000000.Reset();
  heaps_.v40000000.Reset();
  heaps_.v80000000.Reset();
  heaps_.v90000000.Reset();
  heaps_.physical.Reset();
}

const BaseHeap* Memory::LookupHeap(uint32_t address) const {
  if (address < 0x40000000) {
    return &heaps_.v00000000;
  } else if (address < 0x7F000000) {
    return &heaps_.v40000000;
  } else if (address < 0x80000000) {
    return nullptr;
  } else if (address < 0x90000000) {
    return &heaps_.v80000000;
  } else if (address < 0xA0000000) {
    return &heaps_.v90000000;
  } else if (address < 0xC0000000) {
    return &heaps_.vA0000000;
  } else if (address < 0xE0000000) {
    return &heaps_.vC0000000;
  } else if (address < 0xFFD00000) {
    return &heaps_.vE0000000;
  } else {
    return nullptr;
  }
}

BaseHeap* Memory::LookupHeapByType(bool physical, uint32_t page_size) {
  if (physical) {
    if (page_size <= 4096) {
      return &heaps_.vE0000000;
    } else if (page_size <= 64 * 1024) {
      return &heaps_.vA0000000;
    } else {
      return &heaps_.vC0000000;
    }
  } else {
    if (page_size <= 4096) {
      return &heaps_.v00000000;
    } else {
      return &heaps_.v40000000;
    }
  }
}

VirtualHeap* Memory::GetPhysicalHeap() { return &heaps_.physical; }

void Memory::GetHeapsPageStatsSummary(const BaseHeap* const* provided_heaps,
                                      size_t heaps_count,
                                      uint32_t& unreserved_pages,
                                      uint32_t& reserved_pages,
                                      uint32_t& used_pages,
                                      uint32_t& reserved_bytes) {
  auto lock = global_critical_region_.Acquire();
  for (size_t i = 0; i < heaps_count; i++) {
    const BaseHeap* heap = provided_heaps[i];
    uint32_t heap_unreserved_pages = heap->unreserved_page_count();
    uint32_t heap_reserved_pages = heap->reserved_page_count();

    unreserved_pages += heap_unreserved_pages;
    reserved_pages += heap_reserved_pages;
    used_pages += ((heap->total_page_count() - heap_unreserved_pages) *
                   heap->page_size()) /
                  4096;
    reserved_bytes += heap_reserved_pages * heap->page_size();
  }
}

uint32_t Memory::HostToGuestVirtual(const void* host_address) const {
  size_t virtual_address = reinterpret_cast<size_t>(host_address) -
                           reinterpret_cast<size_t>(virtual_membase_);
  uint32_t vE0000000_host_offset = heaps_.vE0000000.host_address_offset();
  size_t vE0000000_host_base =
      size_t(heaps_.vE0000000.heap_base()) + vE0000000_host_offset;
  if (virtual_address >= vE0000000_host_base &&
      virtual_address <=
          (vE0000000_host_base + (heaps_.vE0000000.heap_size() - 1))) {
    virtual_address -= vE0000000_host_offset;
  }
  return uint32_t(virtual_address);
}

uint32_t Memory::HostToGuestVirtualThunk(const void* context,
                                         const void* host_address) {
  return reinterpret_cast<const Memory*>(context)->HostToGuestVirtual(
      host_address);
}

uint32_t Memory::GetPhysicalAddress(uint32_t address) const {
  const BaseHeap* heap = LookupHeap(address);
  if (!heap || heap->heap_type() != HeapType::kGuestPhysical) {
    return UINT32_MAX;
  }
  return static_cast<const PhysicalHeap*>(heap)->GetPhysicalAddress(address);
}

void Memory::Zero(uint32_t address, uint32_t size) {
  std::memset(TranslateVirtual(address), 0, size);
}

void Memory::Fill(uint32_t address, uint32_t size, uint8_t value) {
  std::memset(TranslateVirtual(address), value, size);
}

void Memory::Copy(uint32_t dest, uint32_t src, uint32_t size) {
  uint8_t* pdest = TranslateVirtual(dest);
  const uint8_t* psrc = TranslateVirtual(src);
  std::memcpy(pdest, psrc, size);
}

uint32_t Memory::SearchAligned(uint32_t start, uint32_t end,
                               const uint32_t* values, size_t value_count) {
  assert_true(start <= end);
  auto p = TranslateVirtual<const uint32_t*>(start);
  auto pe = TranslateVirtual<const uint32_t*>(end);
  while (p != pe) {
    if (*p == values[0]) {
      const uint32_t* pc = p + 1;
      size_t matched = 1;
      for (size_t n = 1; n < value_count; n++, pc++) {
        if (*pc != values[n]) {
          break;
        }
        matched++;
      }
      if (matched == value_count) {
        return HostToGuestVirtual(p);
      }
    }
    p++;
  }
  return 0;
}

bool Memory::AddVirtualMappedRange(uint32_t virtual_address, uint32_t mask,
                                   uint32_t size, void* context,
                                   cpu::MMIOReadCallback read_callback,
                                   cpu::MMIOWriteCallback write_callback) {
  if (!xe::memory::AllocFixed(TranslateVirtual(virtual_address), size,
                              xe::memory::AllocationType::kCommit,
                              xe::memory::PageAccess::kNoAccess)) {
    XELOGE("Unable to map range; commit/protect failed");
    return false;
  }
  return mmio_handler_->RegisterRange(virtual_address, mask, size, context,
                                      read_callback, write_callback);
}

cpu::MMIORange* Memory::LookupVirtualMappedRange(uint32_t virtual_address) {
  return mmio_handler_->LookupRange(virtual_address);
}

bool Memory::AccessViolationCallback(
    std::unique_lock<std::recursive_mutex> global_lock_locked_once,
    void* host_address, bool is_write) {
  // Access via physical_membase_ is special, when need to bypass everything
  // (for instance, for a data provider to actually write the data) so only
  // triggering callbacks on virtual memory regions.
  if (reinterpret_cast<size_t>(host_address) <
          reinterpret_cast<size_t>(virtual_membase_) ||
      reinterpret_cast<size_t>(host_address) >=
          reinterpret_cast<size_t>(physical_membase_)) {
    return false;
  }
  uint32_t virtual_address = HostToGuestVirtual(host_address);
  BaseHeap* heap = LookupHeap(virtual_address);
  if (heap->heap_type() != HeapType::kGuestPhysical) {
    return false;
  }

  // Access violation callbacks from the guest are triggered when the global
  // critical region mutex is locked once.
  //
  // Will be rounded to physical page boundaries internally, so just pass 1 as
  // the length - guranteed not to cross page boundaries also.
  auto physical_heap = static_cast<PhysicalHeap*>(heap);
  return physical_heap->TriggerCallbacks(std::move(global_lock_locked_once),
                                         virtual_address, 1, is_write, false);
}

bool Memory::AccessViolationCallbackThunk(
    std::unique_lock<std::recursive_mutex> global_lock_locked_once,
    void* context, void* host_address, bool is_write) {
  return reinterpret_cast<Memory*>(context)->AccessViolationCallback(
      std::move(global_lock_locked_once), host_address, is_write);
}

bool Memory::TriggerPhysicalMemoryCallbacks(
    std::unique_lock<std::recursive_mutex> global_lock_locked_once,
    uint32_t virtual_address, uint32_t length, bool is_write,
    bool unwatch_exact_range, bool unprotect) {
  BaseHeap* heap = LookupHeap(virtual_address);
  if (heap->heap_type() == HeapType::kGuestPhysical) {
    auto physical_heap = static_cast<PhysicalHeap*>(heap);
    return physical_heap->TriggerCallbacks(std::move(global_lock_locked_once),
                                           virtual_address, length, is_write,
                                           unwatch_exact_range, unprotect);
  }
  return false;
}

void* Memory::RegisterPhysicalMemoryInvalidationCallback(
    PhysicalMemoryInvalidationCallback callback, void* callback_context) {
  auto entry = new std::pair<PhysicalMemoryInvalidationCallback, void*>(
      callback, callback_context);
  auto lock = global_critical_region_.Acquire();
  physical_memory_invalidation_callbacks_.push_back(entry);
  return entry;
}

void Memory::UnregisterPhysicalMemoryInvalidationCallback(
    void* callback_handle) {
  auto entry =
      reinterpret_cast<std::pair<PhysicalMemoryInvalidationCallback, void*>*>(
          callback_handle);
  {
    auto lock = global_critical_region_.Acquire();
    auto it = std::find(physical_memory_invalidation_callbacks_.begin(),
                        physical_memory_invalidation_callbacks_.end(), entry);
    assert_true(it != physical_memory_invalidation_callbacks_.end());
    if (it != physical_memory_invalidation_callbacks_.end()) {
      physical_memory_invalidation_callbacks_.erase(it);
    }
  }
  delete entry;
}

void Memory::EnablePhysicalMemoryAccessCallbacks(
    uint32_t physical_address, uint32_t length,
    bool enable_invalidation_notifications, bool enable_data_providers) {
  heaps_.vA0000000.EnableAccessCallbacks(physical_address, length,
                                         enable_invalidation_notifications,
                                         enable_data_providers);
  heaps_.vC0000000.EnableAccessCallbacks(physical_address, length,
                                         enable_invalidation_notifications,
                                         enable_data_providers);
  heaps_.vE0000000.EnableAccessCallbacks(physical_address, length,
                                         enable_invalidation_notifications,
                                         enable_data_providers);
}

uint32_t Memory::SystemHeapAlloc(uint32_t size, uint32_t alignment,
                                 uint32_t system_heap_flags) {
  // TODO(benvanik): lightweight pool.
  bool is_physical = !!(system_heap_flags & kSystemHeapPhysical);
  auto heap = LookupHeapByType(is_physical, 4096);
  uint32_t address;
  if (!heap->Alloc(size, alignment,
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite, false, &address)) {
    return 0;
  }
  Zero(address, size);
  return address;
}

void Memory::SystemHeapFree(uint32_t address) {
  if (!address) {
    return;
  }
  // TODO(benvanik): lightweight pool.
  auto heap = LookupHeap(address);
  heap->Release(address);
}

void Memory::DumpMap() {
  XELOGE("==================================================================");
  XELOGE("Memory Dump");
  XELOGE("==================================================================");
  XELOGE("               System Page Size: {0} ({0:08X})", system_page_size_);
  XELOGE("  System Allocation Granularity: {0} ({0:08X})",
         system_allocation_granularity_);
  XELOGE("                Virtual Membase: {}", virtual_membase_);
  XELOGE("               Physical Membase: {}", physical_membase_);
  XELOGE("");
  XELOGE("------------------------------------------------------------------");
  XELOGE("Virtual Heaps");
  XELOGE("------------------------------------------------------------------");
  XELOGE("");
  heaps_.v00000000.DumpMap();
  heaps_.v40000000.DumpMap();
  heaps_.v80000000.DumpMap();
  heaps_.v90000000.DumpMap();
  XELOGE("");
  XELOGE("------------------------------------------------------------------");
  XELOGE("Physical Heaps");
  XELOGE("------------------------------------------------------------------");
  XELOGE("");
  heaps_.physical.DumpMap();
  heaps_.vA0000000.DumpMap();
  heaps_.vC0000000.DumpMap();
  heaps_.vE0000000.DumpMap();
  XELOGE("");
}

bool Memory::Save(ByteStream* stream) {
  XELOGD("Serializing memory...");
  heaps_.v00000000.Save(stream);
  heaps_.v40000000.Save(stream);
  heaps_.v80000000.Save(stream);
  heaps_.v90000000.Save(stream);
  heaps_.physical.Save(stream);

  return true;
}

bool Memory::Restore(ByteStream* stream) {
  XELOGD("Restoring memory...");
  heaps_.v00000000.Restore(stream);
  heaps_.v40000000.Restore(stream);
  heaps_.v80000000.Restore(stream);
  heaps_.v90000000.Restore(stream);
  heaps_.physical.Restore(stream);

  return true;
}

xe::memory::PageAccess ToPageAccess(uint32_t protect) {
  if ((protect & kMemoryProtectRead) && !(protect & kMemoryProtectWrite)) {
    return xe::memory::PageAccess::kReadOnly;
  } else if ((protect & kMemoryProtectRead) &&
             (protect & kMemoryProtectWrite)) {
    return xe::memory::PageAccess::kReadWrite;
  } else {
    return xe::memory::PageAccess::kNoAccess;
  }
}

uint32_t FromPageAccess(xe::memory::PageAccess protect) {
  switch (protect) {
    case memory::PageAccess::kNoAccess:
      return kMemoryProtectNoAccess;
    case memory::PageAccess::kReadOnly:
      return kMemoryProtectRead;
    case memory::PageAccess::kReadWrite:
      return kMemoryProtectRead | kMemoryProtectWrite;
    case memory::PageAccess::kExecuteReadOnly:
      // Guest memory cannot be executable - this should never happen :)
      assert_always();
      return kMemoryProtectRead;
    case memory::PageAccess::kExecuteReadWrite:
      // Guest memory cannot be executable - this should never happen :)
      assert_always();
      return kMemoryProtectRead | kMemoryProtectWrite;
  }

  return kMemoryProtectNoAccess;
}

BaseHeap::BaseHeap()
    : membase_(nullptr), heap_base_(0), heap_size_(0), page_size_(0) {}

BaseHeap::~BaseHeap() = default;

void BaseHeap::Initialize(Memory* memory, uint8_t* membase, HeapType heap_type,
                          uint32_t heap_base, uint32_t heap_size,
                          uint32_t page_size, uint32_t host_address_offset) {
  memory_ = memory;
  membase_ = membase;
  heap_type_ = heap_type;
  heap_base_ = heap_base;
  heap_size_ = heap_size;
  page_size_ = page_size;
  host_address_offset_ = host_address_offset;
  page_table_.resize(heap_size / page_size);
  unreserved_page_count_ = uint32_t(page_table_.size());
}

void BaseHeap::Dispose() {
  // Walk table and release all regions.
  for (uint32_t page_number = 0; page_number < page_table_.size();
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    if (page_entry.state) {
      xe::memory::DeallocFixed(TranslateRelative(page_number * page_size_), 0,
                               xe::memory::DeallocationType::kRelease);
      page_number += page_entry.region_page_count;
    }
  }
}

void BaseHeap::DumpMap() {
  auto global_lock = global_critical_region_.Acquire();
  XELOGE("------------------------------------------------------------------");
  XELOGE("Heap: {:08X}-{:08X}", heap_base_, heap_base_ + (heap_size_ - 1));
  XELOGE("------------------------------------------------------------------");
  XELOGE("            Heap Base: {:08X}", heap_base_);
  XELOGE("            Heap Size: {0} ({0:08X})", heap_size_);
  XELOGE("            Page Size: {0} ({0:08X})", page_size_);
  XELOGE("           Page Count: {}", page_table_.size());
  XELOGE("  Host Address Offset: {0} ({0:08X})", host_address_offset_);
  bool is_empty_span = false;
  uint32_t empty_span_start = 0;
  for (uint32_t i = 0; i < uint32_t(page_table_.size()); ++i) {
    auto& page = page_table_[i];
    if (!page.state) {
      if (!is_empty_span) {
        is_empty_span = true;
        empty_span_start = i;
      }
      continue;
    }
    if (is_empty_span) {
      XELOGE("  {:08X}-{:08X} {:6d}p {:10d}b unreserved",
             heap_base_ + empty_span_start * page_size_,
             heap_base_ + i * page_size_, i - empty_span_start,
             (i - empty_span_start) * page_size_);
      is_empty_span = false;
    }
    const char* state_name = "   ";
    if (page.state & kMemoryAllocationCommit) {
      state_name = "COM";
    } else if (page.state & kMemoryAllocationReserve) {
      state_name = "RES";
    }
    char access_r = (page.current_protect & kMemoryProtectRead) ? 'R' : ' ';
    char access_w = (page.current_protect & kMemoryProtectWrite) ? 'W' : ' ';
    XELOGE("  {:08X}-{:08X} {:6d}p {:10d}b {} {}{}",
           heap_base_ + i * page_size_,
           heap_base_ + (i + page.region_page_count) * page_size_,
           page.region_page_count, page.region_page_count * page_size_,
           state_name, access_r, access_w);
    i += page.region_page_count - 1;
  }
  if (is_empty_span) {
    XELOGE("  {:08X}-{:08X} - {} unreserved pages)",
           heap_base_ + empty_span_start * page_size_,
           heap_base_ + (heap_size_ - 1),
           page_table_.size() - empty_span_start);
  }
}

bool BaseHeap::Save(ByteStream* stream) {
  XELOGD("Heap {:08X}-{:08X}", heap_base_, heap_base_ + (heap_size_ - 1));

  for (size_t i = 0; i < page_table_.size(); i++) {
    auto& page = page_table_[i];
    stream->Write(page.qword);
    if (!page.state) {
      // Unallocated.
      continue;
    }

    // TODO(DrChat): write compressed with snappy.
    if (page.state & kMemoryAllocationCommit) {
      void* addr = TranslateRelative(i * page_size_);

      memory::PageAccess old_access;
      memory::Protect(addr, page_size_, memory::PageAccess::kReadWrite,
                      &old_access);

      stream->Write(addr, page_size_);

      memory::Protect(addr, page_size_, old_access, nullptr);
    }
  }

  return true;
}

bool BaseHeap::Restore(ByteStream* stream) {
  XELOGD("Heap {:08X}-{:08X}", heap_base_, heap_base_ + (heap_size_ - 1));

  for (size_t i = 0; i < page_table_.size(); i++) {
    auto& page = page_table_[i];
    page.qword = stream->Read<uint64_t>();
    if (!page.state) {
      // Unallocated.
      continue;
    }

    memory::PageAccess page_access = memory::PageAccess::kNoAccess;
    if ((page.current_protect & kMemoryProtectRead) &&
        (page.current_protect & kMemoryProtectWrite)) {
      page_access = memory::PageAccess::kReadWrite;
    } else if (page.current_protect & kMemoryProtectRead) {
      page_access = memory::PageAccess::kReadOnly;
    }

    // Commit the memory if it isn't already. We do not need to reserve any
    // memory, as the mapping has already taken care of that.
    if (page.state & kMemoryAllocationCommit) {
      xe::memory::AllocFixed(TranslateRelative(i * page_size_), page_size_,
                             memory::AllocationType::kCommit,
                             memory::PageAccess::kReadWrite);
    }

    // Now read into memory. We'll set R/W protection first, then set the
    // protection back to its previous state.
    // TODO(DrChat): read compressed with snappy.
    if (page.state & kMemoryAllocationCommit) {
      void* addr = TranslateRelative(i * page_size_);
      xe::memory::Protect(addr, page_size_, memory::PageAccess::kReadWrite,
                          nullptr);

      stream->Read(addr, page_size_);

      xe::memory::Protect(addr, page_size_, page_access, nullptr);
    }
  }

  return true;
}

void BaseHeap::Reset() {
  // TODO(DrChat): protect pages.
  std::memset(page_table_.data(), 0, sizeof(PageEntry) * page_table_.size());
  // TODO(Triang3l): Remove access callbacks from pages if this is a physical
  // memory heap.
}

bool BaseHeap::Alloc(uint32_t size, uint32_t alignment,
                     uint32_t allocation_type, uint32_t protect, bool top_down,
                     uint32_t* out_address) {
  *out_address = 0;
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);
  uint32_t low_address = heap_base_;
  uint32_t high_address = heap_base_ + (heap_size_ - 1);
  return AllocRange(low_address, high_address, size, alignment, allocation_type,
                    protect, top_down, out_address);
}

bool BaseHeap::AllocFixed(uint32_t base_address, uint32_t size,
                          uint32_t alignment, uint32_t allocation_type,
                          uint32_t protect) {
  alignment = xe::round_up(alignment, page_size_);
  size = xe::align(size, alignment);
  assert_true(base_address % alignment == 0);
  uint32_t page_count = get_page_count(size, page_size_);
  uint32_t start_page_number = (base_address - heap_base_) / page_size_;
  uint32_t end_page_number = start_page_number + page_count - 1;
  if (start_page_number >= page_table_.size() ||
      end_page_number > page_table_.size()) {
    XELOGE("BaseHeap::AllocFixed passed out of range address range");
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // - If we are reserving the entire range requested must not be already
  //   reserved.
  // - If we are committing it's ok for pages within the range to already be
  //   committed.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    uint32_t state = page_table_[page_number].state;
    if ((allocation_type == kMemoryAllocationReserve) && state) {
      // Already reserved.
      XELOGE(
          "BaseHeap::AllocFixed attempting to reserve an already reserved "
          "range");
      return false;
    }
    if ((allocation_type == kMemoryAllocationCommit) &&
        !(state & kMemoryAllocationReserve)) {
      // Attempting a commit-only op on an unreserved page.
      // This may be OK.
      XELOGW("BaseHeap::AllocFixed attempting commit on unreserved page");
      allocation_type |= kMemoryAllocationReserve;
      break;
    }
  }

  // Allocate from host.
  if (allocation_type == kMemoryAllocationReserve) {
    // Reserve is not needed, as we are mapped already.
  } else {
    auto alloc_type = (allocation_type & kMemoryAllocationCommit)
                          ? xe::memory::AllocationType::kCommit
                          : xe::memory::AllocationType::kReserve;
    void* result = xe::memory::AllocFixed(
        TranslateRelative(start_page_number * page_size_),
        page_count * page_size_, alloc_type, ToPageAccess(protect));
    if (!result) {
      XELOGE("BaseHeap::AllocFixed failed to alloc range from host");
      return false;
    }

    if (cvars::scribble_heap && protect & kMemoryProtectWrite) {
      std::memset(result, 0xCD, page_count * page_size_);
    }
  }

  // Set page state.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    if (allocation_type & kMemoryAllocationReserve) {
      // Region is based on reservation.
      page_entry.base_address = start_page_number;
      page_entry.region_page_count = page_count;
    }
    page_entry.allocation_protect = protect;
    page_entry.current_protect = protect;
    if (!(page_entry.state & kMemoryAllocationReserve)) {
      unreserved_page_count_--;
    }
    page_entry.state = kMemoryAllocationReserve | allocation_type;
  }

  return true;
}

bool BaseHeap::AllocRange(uint32_t low_address, uint32_t high_address,
                          uint32_t size, uint32_t alignment,
                          uint32_t allocation_type, uint32_t protect,
                          bool top_down, uint32_t* out_address) {
  *out_address = 0;

  alignment = xe::round_up(alignment, page_size_);
  uint32_t page_count = get_page_count(size, page_size_);
  low_address = std::max(heap_base_, xe::align(low_address, alignment));
  high_address = std::min(heap_base_ + (heap_size_ - 1),
                          xe::align(high_address, alignment));
  uint32_t low_page_number = (low_address - heap_base_) / page_size_;
  uint32_t high_page_number = (high_address - heap_base_) / page_size_;
  low_page_number = std::min(uint32_t(page_table_.size()) - 1, low_page_number);
  high_page_number =
      std::min(uint32_t(page_table_.size()) - 1, high_page_number);

  if (page_count > (high_page_number - low_page_number)) {
    XELOGE("BaseHeap::Alloc page count too big for requested range");
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Find a free page range.
  // The base page must match the requested alignment, so we first scan for
  // a free aligned page and only then check for continuous free pages.
  // TODO(benvanik): optimized searching (free list buckets, bitmap, etc).
  uint32_t start_page_number = UINT_MAX;
  uint32_t end_page_number = UINT_MAX;
  uint32_t page_scan_stride = alignment / page_size_;
  high_page_number = high_page_number - (high_page_number % page_scan_stride);
  if (top_down) {
    for (int64_t base_page_number =
             high_page_number - xe::round_up(page_count, page_scan_stride);
         base_page_number >= low_page_number;
         base_page_number -= page_scan_stride) {
      if (page_table_[base_page_number].state != 0) {
        // Base page not free, skip to next usable page.
        continue;
      }
      // Check requested range to ensure free.
      start_page_number = uint32_t(base_page_number);
      end_page_number = uint32_t(base_page_number) + page_count - 1;
      assert_true(end_page_number < page_table_.size());
      bool any_taken = false;
      for (uint32_t page_number = uint32_t(base_page_number);
           !any_taken && page_number <= end_page_number; ++page_number) {
        bool is_free = page_table_[page_number].state == 0;
        if (!is_free) {
          // At least one page in the range is used, skip to next.
          // We know we'll be starting at least before this page.
          any_taken = true;
          if (page_count > page_number) {
            // Not enough space left to fit entire page range. Breaks outer
            // loop.
            base_page_number = -1;
          } else {
            base_page_number = page_number - page_count;
            base_page_number -= base_page_number % page_scan_stride;
            base_page_number += page_scan_stride;  // cancel out loop logic
          }
          break;
        }
      }
      if (!any_taken) {
        // Found our place.
        break;
      }
      // Retry.
      start_page_number = end_page_number = UINT_MAX;
    }
  } else {
    for (uint32_t base_page_number = low_page_number;
         base_page_number <= high_page_number - page_count;
         base_page_number += page_scan_stride) {
      if (page_table_[base_page_number].state != 0) {
        // Base page not free, skip to next usable page.
        continue;
      }
      // Check requested range to ensure free.
      start_page_number = base_page_number;
      end_page_number = base_page_number + page_count - 1;
      bool any_taken = false;
      for (uint32_t page_number = base_page_number;
           !any_taken && page_number <= end_page_number; ++page_number) {
        bool is_free = page_table_[page_number].state == 0;
        if (!is_free) {
          // At least one page in the range is used, skip to next.
          // We know we'll be starting at least after this page.
          any_taken = true;
          base_page_number = xe::round_up(page_number + 1, page_scan_stride);
          base_page_number -= page_scan_stride;  // cancel out loop logic
          break;
        }
      }
      if (!any_taken) {
        // Found our place.
        break;
      }
      // Retry.
      start_page_number = end_page_number = UINT_MAX;
    }
  }
  if (start_page_number == UINT_MAX || end_page_number == UINT_MAX) {
    // Out of memory.
    XELOGE("BaseHeap::Alloc failed to find contiguous range");
    assert_always("Heap exhausted!");
    return false;
  }

  // Allocate from host.
  if (allocation_type == kMemoryAllocationReserve) {
    // Reserve is not needed, as we are mapped already.
  } else {
    auto alloc_type = (allocation_type & kMemoryAllocationCommit)
                          ? xe::memory::AllocationType::kCommit
                          : xe::memory::AllocationType::kReserve;
    void* result = xe::memory::AllocFixed(
        TranslateRelative(start_page_number * page_size_),
        page_count * page_size_, alloc_type, ToPageAccess(protect));
    if (!result) {
      XELOGE("BaseHeap::Alloc failed to alloc range from host");
      return false;
    }

    if (cvars::scribble_heap && (protect & kMemoryProtectWrite)) {
      std::memset(result, 0xCD, page_count * page_size_);
    }
  }

  // Set page state.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.base_address = start_page_number;
    page_entry.region_page_count = page_count;
    page_entry.allocation_protect = protect;
    page_entry.current_protect = protect;
    page_entry.state = kMemoryAllocationReserve | allocation_type;
    unreserved_page_count_--;
  }

  *out_address = heap_base_ + (start_page_number * page_size_);
  return true;
}

bool BaseHeap::Decommit(uint32_t address, uint32_t size) {
  uint32_t page_count = get_page_count(size, page_size_);
  uint32_t start_page_number = (address - heap_base_) / page_size_;
  uint32_t end_page_number = start_page_number + page_count - 1;
  start_page_number =
      std::min(uint32_t(page_table_.size()) - 1, start_page_number);
  end_page_number = std::min(uint32_t(page_table_.size()) - 1, end_page_number);

  auto global_lock = global_critical_region_.Acquire();

  // Release from host.
  // TODO(benvanik): find a way to actually decommit memory;
  //     mapped memory cannot be decommitted.
  /*BOOL result =
      VirtualFree(TranslateRelative(start_page_number * page_size_),
                  page_count * page_size_, MEM_DECOMMIT);
  if (!result) {
    PLOGW("BaseHeap::Decommit failed due to host VirtualFree failure");
    return false;
  }*/

  // Perform table change.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.state &= ~kMemoryAllocationCommit;
  }

  return true;
}

bool BaseHeap::Release(uint32_t base_address, uint32_t* out_region_size) {
  auto global_lock = global_critical_region_.Acquire();

  // Given address must be a region base address.
  uint32_t base_page_number = (base_address - heap_base_) / page_size_;
  auto base_page_entry = page_table_[base_page_number];
  if (base_page_entry.base_address != base_page_number) {
    XELOGE("BaseHeap::Release failed because address is not a region start");
    return false;
  }

  if (heap_base_ == 0x00000000 && base_page_number == 0) {
    XELOGE("BaseHeap::Release: Attempt to free 0!");
    return false;
  }

  if (out_region_size) {
    *out_region_size = (base_page_entry.region_page_count * page_size_);
  }

  // Release from host not needed as mapping reserves the range for us.
  // TODO(benvanik): protect with NOACCESS?
  /*BOOL result = VirtualFree(
      TranslateRelative(base_page_number * page_size_), 0, MEM_RELEASE);
  if (!result) {
    PLOGE("BaseHeap::Release failed due to host VirtualFree failure");
    return false;
  }*/
  // Instead, we just protect it, if we can.
  if (page_size_ == xe::memory::page_size() ||
      ((base_page_entry.region_page_count * page_size_) %
               xe::memory::page_size() ==
           0 &&
       ((base_page_number * page_size_) % xe::memory::page_size() == 0))) {
    // TODO(benvanik): figure out why games are using memory after releasing
    // it. It's possible this is some virtual/physical stuff where the GPU
    // still can access it.
    if (cvars::protect_on_release) {
      if (!xe::memory::Protect(TranslateRelative(base_page_number * page_size_),
                               base_page_entry.region_page_count * page_size_,
                               xe::memory::PageAccess::kNoAccess, nullptr)) {
        XELOGW("BaseHeap::Release failed due to host VirtualProtect failure");
      }
    }
  }

  // Perform table change.
  uint32_t end_page_number =
      base_page_number + base_page_entry.region_page_count - 1;
  for (uint32_t page_number = base_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.qword = 0;
    unreserved_page_count_++;
  }

  return true;
}

bool BaseHeap::Protect(uint32_t address, uint32_t size, uint32_t protect,
                       uint32_t* old_protect) {
  if (!size) {
    XELOGE("BaseHeap::Protect failed due to zero size");
    return false;
  }

  // From the VirtualProtect MSDN page:
  //
  // "The region of affected pages includes all pages containing one or more
  //  bytes in the range from the lpAddress parameter to (lpAddress+dwSize).
  //  This means that a 2-byte range straddling a page boundary causes the
  //  protection attributes of both pages to be changed."
  //
  // "The access protection value can be set only on committed pages. If the
  //  state of any page in the specified region is not committed, the function
  //  fails and returns without modifying the access protection of any pages in
  //  the specified region."

  uint32_t start_page_number = (address - heap_base_) / page_size_;
  if (start_page_number >= page_table_.size()) {
    XELOGE("BaseHeap::Protect failed due to out-of-bounds base address {:08X}",
           address);
    return false;
  }
  uint32_t end_page_number =
      uint32_t((uint64_t(address) + size - 1 - heap_base_) / page_size_);
  if (end_page_number >= page_table_.size()) {
    XELOGE(
        "BaseHeap::Protect failed due to out-of-bounds range ({:08X} bytes "
        "from {:08x})",
        size, address);
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Ensure all pages are in the same reserved region and all are committed.
  uint32_t first_base_address = UINT_MAX;
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto page_entry = page_table_[page_number];
    if (first_base_address == UINT_MAX) {
      first_base_address = page_entry.base_address;
    } else if (first_base_address != page_entry.base_address) {
      XELOGE("BaseHeap::Protect failed due to request spanning regions");
      return false;
    }
    if (!(page_entry.state & kMemoryAllocationCommit)) {
      XELOGE("BaseHeap::Protect failed due to uncommitted page");
      return false;
    }
  }

  // Attempt host change (hopefully won't fail).
  // We can only do this if our size matches system page granularity.
  uint32_t page_count = end_page_number - start_page_number + 1;
  if (page_size_ == xe::memory::page_size() ||
      (((page_count * page_size_) % xe::memory::page_size() == 0) &&
       ((start_page_number * page_size_) % xe::memory::page_size() == 0))) {
    memory::PageAccess old_protect_access;
    if (!xe::memory::Protect(TranslateRelative(start_page_number * page_size_),
                             page_count * page_size_, ToPageAccess(protect),
                             old_protect ? &old_protect_access : nullptr)) {
      XELOGE("BaseHeap::Protect failed due to host VirtualProtect failure");
      return false;
    }

    if (old_protect) {
      *old_protect = FromPageAccess(old_protect_access);
    }
  } else {
    XELOGW("BaseHeap::Protect: ignoring request as not 4k page aligned");
    return false;
  }

  // Perform table change.
  for (uint32_t page_number = start_page_number; page_number <= end_page_number;
       ++page_number) {
    auto& page_entry = page_table_[page_number];
    page_entry.current_protect = protect;
  }

  return true;
}

bool BaseHeap::QueryRegionInfo(uint32_t base_address,
                               HeapAllocationInfo* out_info) {
  uint32_t start_page_number = (base_address - heap_base_) / page_size_;
  if (start_page_number > page_table_.size()) {
    XELOGE("BaseHeap::QueryRegionInfo base page out of range");
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  auto start_page_entry = page_table_[start_page_number];
  out_info->base_address = base_address;
  out_info->allocation_base = 0;
  out_info->allocation_protect = 0;
  out_info->region_size = 0;
  out_info->state = 0;
  out_info->protect = 0;
  if (start_page_entry.state) {
    // Committed/reserved region.
    out_info->allocation_base = start_page_entry.base_address * page_size_;
    out_info->allocation_protect = start_page_entry.allocation_protect;
    out_info->allocation_size = start_page_entry.region_page_count * page_size_;
    out_info->state = start_page_entry.state;
    out_info->protect = start_page_entry.current_protect;

    // Scan forward and report the size of the region matching the initial
    // base address's attributes.
    for (uint32_t page_number = start_page_number;
         page_number <
         start_page_entry.base_address + start_page_entry.region_page_count;
         ++page_number) {
      auto page_entry = page_table_[page_number];
      if (page_entry.base_address != start_page_entry.base_address ||
          page_entry.state != start_page_entry.state ||
          page_entry.current_protect != start_page_entry.current_protect) {
        // Different region or different properties within the region; done.
        break;
      }
      out_info->region_size += page_size_;
    }
  } else {
    // Free region.
    for (uint32_t page_number = start_page_number;
         page_number < page_table_.size(); ++page_number) {
      auto page_entry = page_table_[page_number];
      if (page_entry.state) {
        // First non-free page; done with region.
        break;
      }
      out_info->region_size += page_size_;
    }
  }
  return true;
}

bool BaseHeap::QuerySize(uint32_t address, uint32_t* out_size) {
  uint32_t page_number = (address - heap_base_) / page_size_;
  if (page_number > page_table_.size()) {
    XELOGE("BaseHeap::QuerySize base page out of range");
    *out_size = 0;
    return false;
  }
  auto global_lock = global_critical_region_.Acquire();
  auto page_entry = page_table_[page_number];
  *out_size = (page_entry.region_page_count * page_size_);
  return true;
}

bool BaseHeap::QueryBaseAndSize(uint32_t* in_out_address, uint32_t* out_size) {
  uint32_t page_number = (*in_out_address - heap_base_) / page_size_;
  if (page_number > page_table_.size()) {
    XELOGE("BaseHeap::QuerySize base page out of range");
    *out_size = 0;
    return false;
  }
  auto global_lock = global_critical_region_.Acquire();
  auto page_entry = page_table_[page_number];
  *in_out_address = (page_entry.base_address * page_size_);
  *out_size = (page_entry.region_page_count * page_size_);
  return true;
}

bool BaseHeap::QueryProtect(uint32_t address, uint32_t* out_protect) {
  uint32_t page_number = (address - heap_base_) / page_size_;
  if (page_number > page_table_.size()) {
    XELOGE("BaseHeap::QueryProtect base page out of range");
    *out_protect = 0;
    return false;
  }
  auto global_lock = global_critical_region_.Acquire();
  auto page_entry = page_table_[page_number];
  *out_protect = page_entry.current_protect;
  return true;
}

xe::memory::PageAccess BaseHeap::QueryRangeAccess(uint32_t low_address,
                                                  uint32_t high_address) {
  if (low_address > high_address || low_address < heap_base_ ||
      (high_address - heap_base_) >= heap_size_) {
    return xe::memory::PageAccess::kNoAccess;
  }
  uint32_t low_page_number = (low_address - heap_base_) / page_size_;
  uint32_t high_page_number = (high_address - heap_base_) / page_size_;
  uint32_t protect = kMemoryProtectRead | kMemoryProtectWrite;
  {
    auto global_lock = global_critical_region_.Acquire();
    for (uint32_t i = low_page_number; protect && i <= high_page_number; ++i) {
      protect &= page_table_[i].current_protect;
    }
  }
  return ToPageAccess(protect);
}

VirtualHeap::VirtualHeap() = default;

VirtualHeap::~VirtualHeap() = default;

void VirtualHeap::Initialize(Memory* memory, uint8_t* membase,
                             HeapType heap_type, uint32_t heap_base,
                             uint32_t heap_size, uint32_t page_size) {
  BaseHeap::Initialize(memory, membase, heap_type, heap_base, heap_size,
                       page_size);
}

PhysicalHeap::PhysicalHeap() : parent_heap_(nullptr) {}

PhysicalHeap::~PhysicalHeap() = default;

void PhysicalHeap::Initialize(Memory* memory, uint8_t* membase,
                              HeapType heap_type, uint32_t heap_base,
                              uint32_t heap_size, uint32_t page_size,
                              VirtualHeap* parent_heap) {
  uint32_t host_address_offset;
  if (heap_base >= 0xE0000000 &&
      xe::memory::allocation_granularity() > 0x1000) {
    host_address_offset = 0x1000;
  } else {
    host_address_offset = 0;
  }

  BaseHeap::Initialize(memory, membase, heap_type, heap_base, heap_size,
                       page_size, host_address_offset);
  parent_heap_ = parent_heap;
  system_page_size_ = uint32_t(xe::memory::page_size());

  system_page_count_ =
      (size_t(heap_size_) + host_address_offset + (system_page_size_ - 1)) /
      system_page_size_;
  system_page_flags_.resize((system_page_count_ + 63) / 64);
}

bool PhysicalHeap::Alloc(uint32_t size, uint32_t alignment,
                         uint32_t allocation_type, uint32_t protect,
                         bool top_down, uint32_t* out_address) {
  *out_address = 0;

  // Default top-down. Since parent heap is bottom-up this prevents
  // collisions.
  top_down = true;

  // Adjust alignment size our page size differs from the parent.
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);

  auto global_lock = global_critical_region_.Acquire();

  // Allocate from parent heap (gets our physical address in 0-512mb).
  uint32_t parent_heap_start = GetPhysicalAddress(heap_base_);
  uint32_t parent_heap_end = GetPhysicalAddress(heap_base_ + (heap_size_ - 1));
  uint32_t parent_address;
  if (!parent_heap_->AllocRange(parent_heap_start, parent_heap_end, size,
                                alignment, allocation_type, protect, top_down,
                                &parent_address)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to alloc physical memory in parent heap");
    return false;
  }

  // Given the address we've reserved in the parent heap, pin that here.
  // Shouldn't be possible for it to be allocated already.
  uint32_t address = heap_base_ + parent_address - parent_heap_start;
  if (!BaseHeap::AllocFixed(address, size, alignment, allocation_type,
                            protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to pin physical memory in physical heap");
    // TODO(benvanik): don't leak parent memory.
    return false;
  }
  *out_address = address;
  return true;
}

bool PhysicalHeap::AllocFixed(uint32_t base_address, uint32_t size,
                              uint32_t alignment, uint32_t allocation_type,
                              uint32_t protect) {
  // Adjust alignment size our page size differs from the parent.
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);

  auto global_lock = global_critical_region_.Acquire();

  // Allocate from parent heap (gets our physical address in 0-512mb).
  // NOTE: this can potentially overwrite heap contents if there are already
  // committed pages in the requested physical range.
  // TODO(benvanik): flag for ensure-not-committed?
  uint32_t parent_base_address = GetPhysicalAddress(base_address);
  if (!parent_heap_->AllocFixed(parent_base_address, size, alignment,
                                allocation_type, protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to alloc physical memory in parent heap");
    return false;
  }

  // Given the address we've reserved in the parent heap, pin that here.
  // Shouldn't be possible for it to be allocated already.
  uint32_t address =
      heap_base_ + parent_base_address - GetPhysicalAddress(heap_base_);
  if (!BaseHeap::AllocFixed(address, size, alignment, allocation_type,
                            protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to pin physical memory in physical heap");
    // TODO(benvanik): don't leak parent memory.
    return false;
  }

  return true;
}

bool PhysicalHeap::AllocRange(uint32_t low_address, uint32_t high_address,
                              uint32_t size, uint32_t alignment,
                              uint32_t allocation_type, uint32_t protect,
                              bool top_down, uint32_t* out_address) {
  *out_address = 0;

  // Adjust alignment size our page size differs from the parent.
  size = xe::round_up(size, page_size_);
  alignment = xe::round_up(alignment, page_size_);

  auto global_lock = global_critical_region_.Acquire();

  // Allocate from parent heap (gets our physical address in 0-512mb).
  low_address = std::max(heap_base_, low_address);
  high_address = std::min(heap_base_ + (heap_size_ - 1), high_address);
  uint32_t parent_low_address = GetPhysicalAddress(low_address);
  uint32_t parent_high_address = GetPhysicalAddress(high_address);
  uint32_t parent_address;
  if (!parent_heap_->AllocRange(parent_low_address, parent_high_address, size,
                                alignment, allocation_type, protect, top_down,
                                &parent_address)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to alloc physical memory in parent heap");
    return false;
  }

  // Given the address we've reserved in the parent heap, pin that here.
  // Shouldn't be possible for it to be allocated already.
  uint32_t address =
      heap_base_ + parent_address - GetPhysicalAddress(heap_base_);
  if (!BaseHeap::AllocFixed(address, size, alignment, allocation_type,
                            protect)) {
    XELOGE(
        "PhysicalHeap::Alloc unable to pin physical memory in physical heap");
    // TODO(benvanik): don't leak parent memory.
    return false;
  }
  *out_address = address;
  return true;
}

bool PhysicalHeap::Decommit(uint32_t address, uint32_t size) {
  auto global_lock = global_critical_region_.Acquire();

  uint32_t parent_address = GetPhysicalAddress(address);
  if (!parent_heap_->Decommit(parent_address, size)) {
    XELOGE("PhysicalHeap::Decommit failed due to parent heap failure");
    return false;
  }

  // Not caring about the contents anymore.
  TriggerCallbacks(std::move(global_lock), address, size, true, true);

  return BaseHeap::Decommit(address, size);
}

bool PhysicalHeap::Release(uint32_t base_address, uint32_t* out_region_size) {
  auto global_lock = global_critical_region_.Acquire();

  uint32_t parent_base_address = GetPhysicalAddress(base_address);
  if (!parent_heap_->Release(parent_base_address, out_region_size)) {
    XELOGE("PhysicalHeap::Release failed due to parent heap failure");
    return false;
  }

  // Must invalidate here because the range being released may be reused in
  // another mapping of physical memory - but callback flags are set in each
  // heap separately (https://github.com/xenia-project/xenia/issues/1559 -
  // dynamic vertices in Viva Pinata start screen and menu allocated in
  // 0xA0000000 at addresses that overlap intro video textures in 0xE0000000,
  // with the state of the allocator as of February 24th, 2020). If memory is
  // invalidated in Alloc instead, Alloc won't be aware of callbacks enabled in
  // other heaps, thus callback handlers will keep considering this range valid
  // forever.
  uint32_t region_size;
  if (QuerySize(base_address, &region_size)) {
    TriggerCallbacks(std::move(global_lock), base_address, region_size, true,
                     true);
  }

  return BaseHeap::Release(base_address, out_region_size);
}

bool PhysicalHeap::Protect(uint32_t address, uint32_t size, uint32_t protect,
                           uint32_t* old_protect) {
  auto global_lock = global_critical_region_.Acquire();

  // Only invalidate if making writable again, for simplicity - not when simply
  // marking some range as immutable, for instance.
  if (protect & kMemoryProtectWrite) {
    TriggerCallbacks(std::move(global_lock), address, size, true, true, false);
  }

  if (!parent_heap_->Protect(GetPhysicalAddress(address), size, protect,
                             old_protect)) {
    XELOGE("PhysicalHeap::Protect failed due to parent heap failure");
    return false;
  }

  return BaseHeap::Protect(address, size, protect);
}

void PhysicalHeap::EnableAccessCallbacks(uint32_t physical_address,
                                         uint32_t length,
                                         bool enable_invalidation_notifications,
                                         bool enable_data_providers) {
  // TODO(Triang3l): Implement data providers.
  assert_false(enable_data_providers);
  if (!enable_invalidation_notifications && !enable_data_providers) {
    return;
  }
  uint32_t physical_address_offset = GetPhysicalAddress(heap_base_);
  if (physical_address < physical_address_offset) {
    if (physical_address_offset - physical_address >= length) {
      return;
    }
    length -= physical_address_offset - physical_address;
    physical_address = physical_address_offset;
  }
  uint32_t heap_relative_address = physical_address - physical_address_offset;
  if (heap_relative_address >= heap_size_) {
    return;
  }
  length = std::min(length, heap_size_ - heap_relative_address);
  if (length == 0) {
    return;
  }

  uint32_t system_page_first =
      (heap_relative_address + host_address_offset()) / system_page_size_;
  uint32_t system_page_last =
      (heap_relative_address + length - 1 + host_address_offset()) /
      system_page_size_;
  system_page_last = std::min(system_page_last, system_page_count_ - 1);
  assert_true(system_page_first <= system_page_last);

  // Update callback flags for system pages and make their protection stricter
  // if needed.
  xe::memory::PageAccess protect_access =
      enable_data_providers ? xe::memory::PageAccess::kNoAccess
                            : xe::memory::PageAccess::kReadOnly;
  uint8_t* protect_base = membase_ + heap_base_;
  uint32_t protect_system_page_first = UINT32_MAX;
  auto global_lock = global_critical_region_.Acquire();
  for (uint32_t i = system_page_first; i <= system_page_last; ++i) {
    // Check if need to enable callbacks for the page and raise its protection.
    //
    // If enabling invalidation notifications:
    // - Page writable and not watched for changes yet - protect and enable
    //   invalidation notifications.
    // - Page seen as writable by the guest, but only needs data providers -
    //   just set the bits to enable invalidation notifications (already has
    //   even stricter protection than needed).
    // - Page not writable as requested by the game - don't do anything (need
    //   real access violations here).
    // If enabling data providers:
    // - Page accessible (either read/write or read-only) and didn't need data
    //   providers initially - protect and enable data providers.
    // - Otherwise - do nothing.
    //
    // It's safe not to await data provider completion here before protecting as
    // this never makes protection lighter, so it can't interfere with page
    // faults that await data providers.
    //
    // Enabling data providers doesn't need to be deferred - providers will be
    // polled for the last time without releasing the lock.
    SystemPageFlagsBlock& page_flags_block = system_page_flags_[i >> 6];
    uint64_t page_flags_bit = uint64_t(1) << (i & 63);
    uint32_t guest_page_number =
        xe::sat_sub(i * system_page_size_, host_address_offset()) / page_size_;
    xe::memory::PageAccess current_page_access =
        ToPageAccess(page_table_[guest_page_number].current_protect);
    bool protect_system_page = false;
    // Don't do anything with inaccessible pages - don't protect, don't enable
    // callbacks - because real access violations are needed there. And don't
    // enable invalidation notifications for read-only pages for the same
    // reason.
    if (current_page_access != xe::memory::PageAccess::kNoAccess) {
      // TODO(Triang3l): Enable data providers.
      if (enable_invalidation_notifications) {
        if (current_page_access != xe::memory::PageAccess::kReadOnly &&
            (page_flags_block.notify_on_invalidation & page_flags_bit) == 0) {
          // TODO(Triang3l): Check if data providers are already enabled.
          // If data providers are already enabled for the page, it has even
          // stricter protection.
          protect_system_page = true;
          page_flags_block.notify_on_invalidation |= page_flags_bit;
        }
      }
    }
    if (protect_system_page) {
      if (protect_system_page_first == UINT32_MAX) {
        protect_system_page_first = i;
      }
    } else {
      if (protect_system_page_first != UINT32_MAX) {
        xe::memory::Protect(
            protect_base + protect_system_page_first * system_page_size_,
            (i - protect_system_page_first) * system_page_size_,
            protect_access);
        protect_system_page_first = UINT32_MAX;
      }
    }
  }
  if (protect_system_page_first != UINT32_MAX) {
    xe::memory::Protect(
        protect_base + protect_system_page_first * system_page_size_,
        (system_page_last + 1 - protect_system_page_first) * system_page_size_,
        protect_access);
  }
}

bool PhysicalHeap::TriggerCallbacks(
    std::unique_lock<std::recursive_mutex> global_lock_locked_once,
    uint32_t virtual_address, uint32_t length, bool is_write,
    bool unwatch_exact_range, bool unprotect) {
  // TODO(Triang3l): Support read watches.
  assert_true(is_write);
  if (!is_write) {
    return false;
  }

  if (virtual_address < heap_base_) {
    if (heap_base_ - virtual_address >= length) {
      return false;
    }
    length -= heap_base_ - virtual_address;
    virtual_address = heap_base_;
  }
  uint32_t heap_relative_address = virtual_address - heap_base_;
  if (heap_relative_address >= heap_size_) {
    return false;
  }
  length = std::min(length, heap_size_ - heap_relative_address);
  if (length == 0) {
    return false;
  }

  uint32_t system_page_first =
      (heap_relative_address + host_address_offset()) / system_page_size_;
  uint32_t system_page_last =
      (heap_relative_address + length - 1 + host_address_offset()) /
      system_page_size_;
  system_page_last = std::min(system_page_last, system_page_count_ - 1);
  assert_true(system_page_first <= system_page_last);
  uint32_t block_index_first = system_page_first >> 6;
  uint32_t block_index_last = system_page_last >> 6;

  // Check if watching any page, whether need to call the callback at all.
  bool any_watched = false;
  for (uint32_t i = block_index_first; i <= block_index_last; ++i) {
    uint64_t block = system_page_flags_[i].notify_on_invalidation;
    if (i == block_index_first) {
      block &= ~((uint64_t(1) << (system_page_first & 63)) - 1);
    }
    if (i == block_index_last && (system_page_last & 63) != 63) {
      block &= (uint64_t(1) << ((system_page_last & 63) + 1)) - 1;
    }
    if (block) {
      any_watched = true;
      break;
    }
  }
  if (!any_watched) {
    return false;
  }

  // Trigger callbacks.
  if (!unprotect) {
    // If not doing anything with protection, no point in unwatching excess
    // pages.
    unwatch_exact_range = true;
  }
  uint32_t physical_address_offset = GetPhysicalAddress(heap_base_);
  uint32_t physical_address_start =
      xe::sat_sub(system_page_first * system_page_size_,
                  host_address_offset()) +
      physical_address_offset;
  uint32_t physical_length = std::min(
      xe::sat_sub(system_page_last * system_page_size_ + system_page_size_,
                  host_address_offset()) +
          physical_address_offset - physical_address_start,
      heap_size_ - (physical_address_start - physical_address_offset));
  uint32_t unwatch_first = 0;
  uint32_t unwatch_last = UINT32_MAX;
  for (auto invalidation_callback :
       memory_->physical_memory_invalidation_callbacks_) {
    std::pair<uint32_t, uint32_t> callback_unwatch_range =
        invalidation_callback->first(invalidation_callback->second,
                                     physical_address_start, physical_length,
                                     unwatch_exact_range);
    if (!unwatch_exact_range) {
      unwatch_first = std::max(unwatch_first, callback_unwatch_range.first);
      unwatch_last = std::min(
          unwatch_last,
          xe::sat_add(
              callback_unwatch_range.first,
              std::max(callback_unwatch_range.second, uint32_t(1)) - 1));
    }
  }
  if (!unwatch_exact_range) {
    // Always unwatch at least the requested pages.
    unwatch_first = std::min(unwatch_first, physical_address_start);
    unwatch_last =
        std::max(unwatch_last, physical_address_start + physical_length - 1);
    // Don't unprotect too much if not caring much about the region (limit to
    // 4 MB - somewhat random, but max 1024 iterations of the page loop).
    const uint32_t kMaxUnwatchExcess = 4 * 1024 * 1024;
    unwatch_first = std::max(unwatch_first,
                             physical_address_start & ~(kMaxUnwatchExcess - 1));
    unwatch_last =
        std::min(unwatch_last, (physical_address_start + physical_length - 1) |
                                   (kMaxUnwatchExcess - 1));
    // Convert to heap-relative addresses.
    unwatch_first = xe::sat_sub(unwatch_first, physical_address_offset);
    unwatch_last = xe::sat_sub(unwatch_last, physical_address_offset);
    // Clamp to the heap upper bound.
    unwatch_first = std::min(unwatch_first, heap_size_ - 1);
    unwatch_last = std::min(unwatch_last, heap_size_ - 1);
    // Convert to system pages and update the range.
    unwatch_first += host_address_offset();
    unwatch_last += host_address_offset();
    assert_true(unwatch_first <= unwatch_last);
    system_page_first = unwatch_first / system_page_size_;
    system_page_last = unwatch_last / system_page_size_;
    block_index_first = system_page_first >> 6;
    block_index_last = system_page_last >> 6;
  }

  // Unprotect ranges that need unprotection.
  if (unprotect) {
    uint8_t* protect_base = membase_ + heap_base_;
    uint32_t unprotect_system_page_first = UINT32_MAX;
    for (uint32_t i = system_page_first; i <= system_page_last; ++i) {
      // Check if need to allow writing to this page.
      bool unprotect_page = (system_page_flags_[i >> 6].notify_on_invalidation &
                             (uint64_t(1) << (i & 63))) != 0;
      if (unprotect_page) {
        uint32_t guest_page_number =
            xe::sat_sub(i * system_page_size_, host_address_offset()) /
            page_size_;
        if (ToPageAccess(page_table_[guest_page_number].current_protect) !=
            xe::memory::PageAccess::kReadWrite) {
          unprotect_page = false;
        }
      }
      if (unprotect_page) {
        if (unprotect_system_page_first == UINT32_MAX) {
          unprotect_system_page_first = i;
        }
      } else {
        if (unprotect_system_page_first != UINT32_MAX) {
          xe::memory::Protect(
              protect_base + unprotect_system_page_first * system_page_size_,
              (i - unprotect_system_page_first) * system_page_size_,
              xe::memory::PageAccess::kReadWrite);
          unprotect_system_page_first = UINT32_MAX;
        }
      }
    }
    if (unprotect_system_page_first != UINT32_MAX) {
      xe::memory::Protect(
          protect_base + unprotect_system_page_first * system_page_size_,
          (system_page_last + 1 - unprotect_system_page_first) *
              system_page_size_,
          xe::memory::PageAccess::kReadWrite);
    }
  }

  // Mark pages as not write-watched.
  for (uint32_t i = block_index_first; i <= block_index_last; ++i) {
    uint64_t mask = 0;
    if (i == block_index_first) {
      mask |= (uint64_t(1) << (system_page_first & 63)) - 1;
    }
    if (i == block_index_last && (system_page_last & 63) != 63) {
      mask |= ~((uint64_t(1) << ((system_page_last & 63) + 1)) - 1);
    }
    system_page_flags_[i].notify_on_invalidation &= mask;
  }

  return true;
}

uint32_t PhysicalHeap::GetPhysicalAddress(uint32_t address) const {
  assert_true(address >= heap_base_);
  address -= heap_base_;
  assert_true(address < heap_size_);
  if (heap_base_ >= 0xE0000000) {
    address += 0x1000;
  }
  return address;
}

}  // namespace xe
