/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_MEMORY_H_
#define XENIA_MEMORY_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/mmio_handler.h"

namespace xe {
class ByteStream;
}  // namespace xe

namespace xe {

class Memory;

enum SystemHeapFlag : uint32_t {
  kSystemHeapVirtual = 1 << 0,
  kSystemHeapPhysical = 1 << 1,

  kSystemHeapDefault = kSystemHeapVirtual,
};

enum class HeapType : uint8_t {
  kGuestVirtual,
  kGuestXex,
  kGuestPhysical,
  kHostPhysical,
};

enum MemoryAllocationFlag : uint32_t {
  kMemoryAllocationReserve = 1 << 0,
  kMemoryAllocationCommit = 1 << 1,
};

enum MemoryProtectFlag : uint32_t {
  kMemoryProtectRead = 1 << 0,
  kMemoryProtectWrite = 1 << 1,
  kMemoryProtectNoCache = 1 << 2,
  kMemoryProtectWriteCombine = 1 << 3,

  kMemoryProtectNoAccess = 0,
};

// Equivalent to the Win32 MEMORY_BASIC_INFORMATION struct.
struct HeapAllocationInfo {
  // A pointer to the base address of the region of pages.
  uint32_t base_address;
  // A pointer to the base address of a range of pages allocated by the
  // VirtualAlloc function. The page pointed to by the BaseAddress member is
  // contained within this allocation range.
  uint32_t allocation_base;
  // The memory protection option when the region was initially allocated.
  uint32_t allocation_protect;
  // The size specified when the region was initially allocated, in bytes.
  uint32_t allocation_size;
  // The size of the region beginning at the base address in which all pages
  // have identical attributes, in bytes.
  uint32_t region_size;
  // The state of the pages in the region (commit/free/reserve).
  uint32_t state;
  // The access protection of the pages in the region.
  uint32_t protect;
};

// Describes a single page in the page table.
union PageEntry {
  struct {
    // Base address of the allocated region in 4k pages.
    uint32_t base_address : 20;
    // Total number of pages in the allocated region in 4k pages.
    uint32_t region_page_count : 20;
    // Protection bits specified during region allocation.
    // Composed of bits from MemoryProtectFlag.
    uint32_t allocation_protect : 4;
    // Current protection bits as of the last Protect.
    // Composed of bits from MemoryProtectFlag.
    uint32_t current_protect : 4;
    // Allocation state of the page as a MemoryAllocationFlag bit mask.
    uint32_t state : 2;
    uint32_t reserved : 14;
  };
  uint64_t qword;
};

// Heap abstraction for page-based allocation.
class BaseHeap {
 public:
  virtual ~BaseHeap();

  // Offset of the heap in relative to membase, without host_address_offset
  // adjustment.
  uint32_t heap_base() const { return heap_base_; }

  // Length of the heap range.
  uint32_t heap_size() const { return heap_size_; }

  // Size of each page within the heap range in bytes.
  uint32_t page_size() const { return page_size_; }

  // Amount of pages assigned to heap
  uint32_t total_page_count() const { return uint32_t(page_table_.size()); }

  // Sum of unreserved pages in heap
  uint32_t unreserved_page_count() const { return unreserved_page_count_; }

  // Sum of reserved pages in heap
  uint32_t reserved_page_count() const {
    return total_page_count() - unreserved_page_count();
  }

  // Type of specified heap
  HeapType heap_type() const { return heap_type_; }

  // Offset added to the virtual addresses to convert them to host addresses
  // (not including membase).
  uint32_t host_address_offset() const { return host_address_offset_; }

  template <typename T = uint8_t*>
  inline T TranslateRelative(size_t relative_address) const {
    return reinterpret_cast<T>(membase_ + heap_base_ + host_address_offset_ +
                               relative_address);
  }

  // Disposes and decommits all memory and clears the page table.
  virtual void Dispose();

  // Dumps information about all allocations within the heap to the log.
  void DumpMap();

  // Allocates pages with the given properties and allocation strategy.
  // This can reserve and commit the pages as well as set protection modes.
  // This will fail if not enough contiguous pages can be found.
  virtual bool Alloc(uint32_t size, uint32_t alignment,
                     uint32_t allocation_type, uint32_t protect, bool top_down,
                     uint32_t* out_address);

  // Allocates pages at the given address.
  // This can reserve and commit the pages as well as set protection modes.
  // This will fail if the pages are already allocated.
  virtual bool AllocFixed(uint32_t base_address, uint32_t size,
                          uint32_t alignment, uint32_t allocation_type,
                          uint32_t protect);

  // Allocates pages at an address within the given address range.
  // This can reserve and commit the pages as well as set protection modes.
  // This will fail if not enough contiguous pages can be found.
  virtual bool AllocRange(uint32_t low_address, uint32_t high_address,
                          uint32_t size, uint32_t alignment,
                          uint32_t allocation_type, uint32_t protect,
                          bool top_down, uint32_t* out_address);

  // Decommits pages in the given range.
  // Partial overlapping pages will also be decommitted.
  virtual bool Decommit(uint32_t address, uint32_t size);

  // Decommits and releases pages in the given range.
  // Partial overlapping pages will also be released.
  virtual bool Release(uint32_t address, uint32_t* out_region_size = nullptr);

  // Modifies the protection mode of pages within the given range.
  virtual bool Protect(uint32_t address, uint32_t size, uint32_t protect,
                       uint32_t* old_protect = nullptr);

  // Queries information about the given region of pages.
  bool QueryRegionInfo(uint32_t base_address, HeapAllocationInfo* out_info);

  // Queries the size of the region containing the given address.
  bool QuerySize(uint32_t address, uint32_t* out_size);

  // Queries the base and size of a region containing the given address.
  bool QueryBaseAndSize(uint32_t* in_out_address, uint32_t* out_size);

  // Queries the current protection mode of the region containing the given
  // address.
  bool QueryProtect(uint32_t address, uint32_t* out_protect);

  // Queries the currently strictest readability and writability for the entire
  // range.
  xe::memory::PageAccess QueryRangeAccess(uint32_t low_address,
                                          uint32_t high_address);

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

  void Reset();

 protected:
  BaseHeap();

  void Initialize(Memory* memory, uint8_t* membase, HeapType heap_type,
                  uint32_t heap_base, uint32_t heap_size, uint32_t page_size,
                  uint32_t host_address_offset = 0);

  Memory* memory_;
  uint8_t* membase_;
  HeapType heap_type_;
  uint32_t heap_base_;
  uint32_t heap_size_;
  uint32_t page_size_;
  uint32_t host_address_offset_;
  uint32_t unreserved_page_count_;
  xe::global_critical_region global_critical_region_;
  std::vector<PageEntry> page_table_;
};

// Normal heap allowing allocations from guest virtual address ranges.
class VirtualHeap : public BaseHeap {
 public:
  VirtualHeap();
  ~VirtualHeap() override;

  // Initializes the heap properties and allocates the page table.
  void Initialize(Memory* memory, uint8_t* membase, HeapType heap_type,
                  uint32_t heap_base, uint32_t heap_size, uint32_t page_size);
};

// A heap for ranges of memory that are mapped to physical ranges.
// Physical ranges are used by the audio and graphics subsystems representing
// hardware wired directly to memory in the console.
//
// The physical heap and the behavior of sharing pages with virtual pages is
// implemented by having a 'parent' heap that is used to perform allocation in
// the guest virtual address space 1:1 with the physical address space.
class PhysicalHeap : public BaseHeap {
 public:
  PhysicalHeap();
  ~PhysicalHeap() override;

  // Initializes the heap properties and allocates the page table.
  void Initialize(Memory* memory, uint8_t* membase, HeapType heap_type,
                  uint32_t heap_base, uint32_t heap_size, uint32_t page_size,
                  VirtualHeap* parent_heap);

  bool Alloc(uint32_t size, uint32_t alignment, uint32_t allocation_type,
             uint32_t protect, bool top_down, uint32_t* out_address) override;
  bool AllocFixed(uint32_t base_address, uint32_t size, uint32_t alignment,
                  uint32_t allocation_type, uint32_t protect) override;
  bool AllocRange(uint32_t low_address, uint32_t high_address, uint32_t size,
                  uint32_t alignment, uint32_t allocation_type,
                  uint32_t protect, bool top_down,
                  uint32_t* out_address) override;
  bool Decommit(uint32_t address, uint32_t size) override;
  bool Release(uint32_t base_address,
               uint32_t* out_region_size = nullptr) override;
  bool Protect(uint32_t address, uint32_t size, uint32_t protect,
               uint32_t* old_protect = nullptr) override;

  void EnableAccessCallbacks(uint32_t physical_address, uint32_t length,
                             bool enable_invalidation_notifications,
                             bool enable_data_providers);
  // Returns true if any page in the range was watched.
  bool TriggerCallbacks(
      std::unique_lock<std::recursive_mutex> global_lock_locked_once,
      uint32_t virtual_address, uint32_t length, bool is_write,
      bool unwatch_exact_range, bool unprotect = true);

  uint32_t GetPhysicalAddress(uint32_t address) const;

 protected:
  VirtualHeap* parent_heap_;

  uint32_t system_page_size_;
  uint32_t system_page_count_;

  struct SystemPageFlagsBlock {
    // Whether writing to each page should result trigger invalidation
    // callbacks.
    uint64_t notify_on_invalidation;
  };
  // Protected by global_critical_region. Flags for each 64 system pages,
  // interleaved as blocks, so bit scan can be used to quickly extract ranges.
  std::vector<SystemPageFlagsBlock> system_page_flags_;
};

// Models the entire guest memory system on the console.
// This exposes interfaces to both virtual and physical memory and a TLB and
// page table for allocation, mapping, and protection.
//
// The memory is backed by a memory mapped file and is placed at a stable
// fixed address in the host address space (like 0x100000000). This allows
// efficient guest<->host address translations as well as easy sharing of the
// memory across various subsystems.
//
// The guest memory address space is split into several ranges that have varying
// properties such as page sizes, caching strategies, protections, and
// overlap with other ranges. Each range is represented by a BaseHeap of either
// VirtualHeap or PhysicalHeap depending on type. Heaps model the page tables
// and can handle reservation and committing of requested pages.
class Memory {
 public:
  Memory();
  ~Memory();

  // Initializes the memory system.
  // This may fail if the host address space could not be reserved or the
  // mapping to the file system fails.
  bool Initialize();

  // Resets all memory to zero and resets all allocations.
  void Reset();

  // Full file name and path of the memory-mapped file backing all memory.
  const std::filesystem::path& file_name() const { return file_name_; }

  // Base address of virtual memory in the host address space.
  // This is often something like 0x100000000.
  inline uint8_t* virtual_membase() const { return virtual_membase_; }

  // Translates a guest virtual address to a host address that can be accessed
  // as a normal pointer.
  // Note that the contents at the specified host address are big-endian.
  template <typename T = uint8_t*>
  inline T TranslateVirtual(uint32_t guest_address) const {
    uint8_t* host_address = virtual_membase_ + guest_address;
    const auto heap = LookupHeap(guest_address);
    if (heap) {
      host_address += heap->host_address_offset();
    }
    return reinterpret_cast<T>(host_address);
  }

  // Base address of physical memory in the host address space.
  // This is often something like 0x200000000.
  inline uint8_t* physical_membase() const { return physical_membase_; }

  // Translates a guest physical address to a host address that can be accessed
  // as a normal pointer.
  // Note that the contents at the specified host address are big-endian.
  template <typename T = uint8_t*>
  inline T TranslatePhysical(uint32_t guest_address) const {
    return reinterpret_cast<T>(physical_membase_ +
                               (guest_address & 0x1FFFFFFF));
  }

  // Translates a host address to a guest virtual address.
  // Note that the contents at the returned host address are big-endian.
  uint32_t HostToGuestVirtual(const void* host_address) const;

  // Returns the guest physical address for the guest virtual address, or
  // UINT32_MAX if it can't be obtained.
  uint32_t GetPhysicalAddress(uint32_t address) const;

  // Zeros out a range of memory at the given guest address.
  void Zero(uint32_t address, uint32_t size);

  // Fills a range of guest memory with the given byte value.
  void Fill(uint32_t address, uint32_t size, uint8_t value);

  // Copies a non-overlapping range of guest memory (like a memcpy).
  void Copy(uint32_t dest, uint32_t src, uint32_t size);

  // Searches the given range of guest memory for a run of dword values in
  // big-endian order.
  uint32_t SearchAligned(uint32_t start, uint32_t end, const uint32_t* values,
                         size_t value_count);

  // Defines a memory-mapped IO (MMIO) virtual address range that when accessed
  // will trigger the specified read and write callbacks for dword read/writes.
  bool AddVirtualMappedRange(uint32_t virtual_address, uint32_t mask,
                             uint32_t size, void* context,
                             cpu::MMIOReadCallback read_callback,
                             cpu::MMIOWriteCallback write_callback);

  // Gets the defined MMIO range for the given virtual address, if any.
  cpu::MMIORange* LookupVirtualMappedRange(uint32_t virtual_address);

  // Physical memory access callbacks, two types of them.
  //
  // This is simple per-system-page protection without reference counting or
  // stored ranges. Whenever a watched page is accessed, all callbacks for it
  // are triggered. Also the only way to remove callbacks is to trigger them
  // somehow. Since there are no references from pages to individual callbacks,
  // there's no way to disable only a specific callback for a page. Also
  // callbacks may be triggered spuriously, and handlers should properly ignore
  // pages they don't care about.
  //
  // Once callbacks are triggered for a page, the page is not watched anymore
  // until requested again later. It is, however, unwatched only in one guest
  // view of physical memory (because different views may have different
  // protection for the same memory) - but it's rare when the same memory is
  // used with different guest page sizes, and it's okay to fire a callback more
  // than once.
  //
  // Only accessing the guest virtual memory views of physical memory triggers
  // callbacks - data providers, for instance, must write to the host physical
  // heap directly, otherwise their threads may infinitely await themselves.
  //
  // - Invalidation notifications:
  //
  // Protecting from writing. One-shot callbacks for invalidation of various
  // kinds of physical memory caches (such as the GPU copy of the memory).
  //
  // May be triggered for a single page (in case of a write access violation or
  // when need to synchronize data given by data providers) or for multiple
  // pages (like when memory is released, or explicitly to trigger callbacks
  // when host-side code can't rely on regular access violations, like when
  // accessing a file).
  //
  // Since granularity of callbacks is one single page, an invalidation
  // notification handler must invalidate the all the data stored in the touched
  // pages.
  //
  // Because large ranges (like whole framebuffers) may be written to and
  // exceptions are expensive, it's better to unprotect multiple pages as a
  // result of a write access violation, so the shortest common range returned
  // by all the invalidation callbacks (clamped to a sane range and also not to
  // touch pages with provider callbacks) is unprotected.
  //
  // - Data providers:
  //
  // TODO(Triang3l): Implement data providers - more complicated because they
  // will need to be able to release the global lock.

  // Returns start and length of the smallest physical memory region surrounding
  // the watched region that can be safely unwatched, if it doesn't matter,
  // return (0, UINT32_MAX).
  typedef std::pair<uint32_t, uint32_t> (*PhysicalMemoryInvalidationCallback)(
      void* context_ptr, uint32_t physical_address_start, uint32_t length,
      bool exact_range);
  // Returns a handle for unregistering or for skipping one notification handler
  // while triggering data providers.
  void* RegisterPhysicalMemoryInvalidationCallback(
      PhysicalMemoryInvalidationCallback callback, void* callback_context);
  // Unregisters a physical memory invalidation callback previously added with
  // RegisterPhysicalMemoryInvalidationCallback.
  void UnregisterPhysicalMemoryInvalidationCallback(void* callback_handle);

  // Enables physical memory access callbacks for the specified memory range,
  // snapped to system page boundaries.
  void EnablePhysicalMemoryAccessCallbacks(
      uint32_t physical_address, uint32_t length,
      bool enable_invalidation_notifications, bool enable_data_providers);

  // Forces triggering of watch callbacks for a virtual address range if pages
  // are watched there and unwatching them. Returns whether any page was
  // watched. Must be called with global critical region locking depth of 1.
  // TODO(Triang3l): Implement data providers - this is why locking depth of 1
  // will be required in the future.
  bool TriggerPhysicalMemoryCallbacks(
      std::unique_lock<std::recursive_mutex> global_lock_locked_once,
      uint32_t virtual_address, uint32_t length, bool is_write,
      bool unwatch_exact_range, bool unprotect = true);

  // Allocates virtual memory from the 'system' heap.
  // System memory is kept separate from game memory but is still accessible
  // using normal guest virtual addresses. Kernel structures and other internal
  // 'system' allocations should come from this heap when possible.
  uint32_t SystemHeapAlloc(uint32_t size, uint32_t alignment = 0x20,
                           uint32_t system_heap_flags = kSystemHeapDefault);

  // Frees memory allocated with SystemHeapAlloc.
  void SystemHeapFree(uint32_t address);

  // Gets the heap for the address space containing the given address.
  const BaseHeap* LookupHeap(uint32_t address) const;

  inline BaseHeap* LookupHeap(uint32_t address) {
    return const_cast<BaseHeap*>(
        const_cast<const Memory*>(this)->LookupHeap(address));
  }

  // Gets the heap with the given properties.
  BaseHeap* LookupHeapByType(bool physical, uint32_t page_size);

  // Gets the physical base heap.
  VirtualHeap* GetPhysicalHeap();

  void GetHeapsPageStatsSummary(const BaseHeap* const* provided_heaps,
                                size_t heaps_count, uint32_t& unreserved_pages,
                                uint32_t& reserved_pages, uint32_t& used_pages,
                                uint32_t& reserved_bytes);

  // Dumps a map of all allocated memory to the log.
  void DumpMap();

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

 private:
  int MapViews(uint8_t* mapping_base);
  void UnmapViews();

  static uint32_t HostToGuestVirtualThunk(const void* context,
                                          const void* host_address);

  bool AccessViolationCallback(
      std::unique_lock<std::recursive_mutex> global_lock_locked_once,
      void* host_address, bool is_write);
  static bool AccessViolationCallbackThunk(
      std::unique_lock<std::recursive_mutex> global_lock_locked_once,
      void* context, void* host_address, bool is_write);

  std::filesystem::path file_name_;
  uint32_t system_page_size_ = 0;
  uint32_t system_allocation_granularity_ = 0;
  uint8_t* virtual_membase_ = nullptr;
  uint8_t* physical_membase_ = nullptr;

  xe::memory::FileMappingHandle mapping_ =
      xe::memory::kFileMappingHandleInvalid;
  uint8_t* mapping_base_ = nullptr;
  union {
    struct {
      uint8_t* v00000000;
      uint8_t* v40000000;
      uint8_t* v7F000000;
      uint8_t* v80000000;
      uint8_t* v90000000;
      uint8_t* vA0000000;
      uint8_t* vC0000000;
      uint8_t* vE0000000;
      uint8_t* physical;
    };
    uint8_t* all_views[9];
  } views_ = {{0}};

  std::unique_ptr<cpu::MMIOHandler> mmio_handler_;

  struct {
    VirtualHeap v00000000;
    VirtualHeap v40000000;
    VirtualHeap v80000000;
    VirtualHeap v90000000;

    VirtualHeap physical;
    PhysicalHeap vA0000000;
    PhysicalHeap vC0000000;
    PhysicalHeap vE0000000;
  } heaps_;

  friend class BaseHeap;

  friend class PhysicalHeap;
  xe::global_critical_region global_critical_region_;
  std::vector<std::pair<PhysicalMemoryInvalidationCallback, void*>*>
      physical_memory_invalidation_callbacks_;
};

}  // namespace xe

#endif  // XENIA_MEMORY_H_
