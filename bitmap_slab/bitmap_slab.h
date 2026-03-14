// bitmap_slab/bitmap_slab.h
//
// Bitmap-based slab allocator for contiguous N-slot allocation of
// fixed-size objects. Uses a bitmap (1 bit per slot) to track free/used
// state, enabling fast scanning for N consecutive free slots via
// bit-manipulation intrinsics.
//
// NOT thread-safe — designed for single-thread (PMD) use.
//
// Template parameters:
//   ObjSize   — size in bytes of each object slot
//   Allocator — backing allocator (must provide allocate/deallocate)

#ifndef BITMAP_SLAB_BITMAP_SLAB_H_
#define BITMAP_SLAB_BITMAP_SLAB_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace bitmap_slab {

// Default heap allocator for unit tests.
struct StdAllocator {
  void* allocate(std::size_t bytes, std::size_t alignment) {
    return ::operator new(bytes, std::align_val_t{alignment});
  }
  void deallocate(void* ptr, std::size_t alignment) {
    ::operator delete(ptr, std::align_val_t{alignment});
  }
};

// A contiguous span of N objects returned by AllocateN.
// Provides array-style access and knows its own length.
template <typename T>
struct Span {
  T* data;
  std::size_t count;

  T& operator[](std::size_t i) { return data[i]; }
  const T& operator[](std::size_t i) const { return data[i]; }
  T* begin() { return data; }
  T* end() { return data + count; }
  const T* begin() const { return data; }
  const T* end() const { return data + count; }
  explicit operator bool() const { return data != nullptr; }
};

// ---------------------------------------------------------------------------
// bitmap_scan — OVS-style bitmap scanner (free function).
//
// Scans 'bitmap' from bit offset 'start' up to (but excluding) 'end'.
// Returns the bit offset of the lowest-numbered bit set to 'target',
// or 'end' if no such bit exists.
//
// 'target' is typically a compile-time constant so the compiler can
// optimize away the branch.  The inner loop advances one word at a time,
// making this efficient for large bitmaps.
// ---------------------------------------------------------------------------
static constexpr std::size_t kBitmapUlongBits = 64;

inline const uint64_t* bitmap_unit(const uint64_t* bitmap, std::size_t offset) {
  return bitmap + offset / kBitmapUlongBits;
}

inline std::size_t bitmap_scan(const uint64_t* bitmap, bool target,
                               std::size_t start, std::size_t end) {
  if (__builtin_expect(start < end, 1)) {
    const uint64_t* p = bitmap_unit(bitmap, start);
    uint64_t unit = (target ? *p : ~*p) >> (start % kBitmapUlongBits);

    if (!unit) {
      start -= start % kBitmapUlongBits;  // Round down.
      start += kBitmapUlongBits;          // Start of the next unit.
      for (; start < end; start += kBitmapUlongBits) {
        unit = target ? *++p : ~*++p;
        if (unit) {
          goto found;
        }
      }
      return end;
    }
  found:
    start += static_cast<std::size_t>(__builtin_ctzll(unit));
    if (__builtin_expect(start < end, 1)) {
      return start;
    }
  }
  return end;
}

template <std::size_t ObjSize, typename Allocator = StdAllocator>
class BitmapSlab {

 public:
  // Construct a slab with the given capacity (number of object slots).
  // capacity is rounded up to a multiple of 64 for bitmap alignment.
  explicit BitmapSlab(std::size_t capacity);
  ~BitmapSlab();

  BitmapSlab(const BitmapSlab&) = delete;
  BitmapSlab& operator=(const BitmapSlab&) = delete;
  BitmapSlab(BitmapSlab&&) = delete;
  BitmapSlab& operator=(BitmapSlab&&) = delete;

  // Allocate N contiguous slots. Returns a Span<T> pointing to the first
  // object, or a null span if no contiguous run of N free slots exists.
  // Does NOT construct objects — caller must placement-new if needed.
  template <typename T>
  Span<T> AllocateN(std::size_t n);

  // Free N contiguous slots starting at ptr.
  // Caller must have previously allocated exactly N slots at this address.
  template <typename T>
  void DeallocateN(T* ptr, std::size_t n);

  // Single-object convenience wrappers.
  template <typename T>
  T* Allocate();

  template <typename T>
  void Deallocate(T* ptr);

  std::size_t capacity() const { return capacity_; }
  std::size_t free_count() const { return free_count_; }
  std::size_t used_count() const { return capacity_ - free_count_; }
  const uint8_t* slab_base() const { return slab_; }

 private:
  // Find the first run of n consecutive set bits in the bitmap.
  // Returns the starting bit index, or capacity_ if not found.
  std::size_t FindContiguousFree(std::size_t n) const;

  // Mark bits [start, start+n) as used (0).
  void MarkUsed(std::size_t start, std::size_t n);

  // Mark bits [start, start+n) as free (1).
  void MarkFree(std::size_t start, std::size_t n);

  // Slot index from pointer.
  std::size_t SlotIndex(const void* ptr) const;

  uint8_t* slab_;              // contiguous backing memory
  uint64_t* bitmap_;           // 1 = free, 0 = used
  std::size_t capacity_;       // user-requested capacity
  std::size_t internal_cap_;   // rounded up to multiple of 64
  std::size_t num_words_;      // number of 64-bit words in bitmap
  std::size_t free_count_;
  Allocator allocator_;
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

template <std::size_t ObjSize, typename Allocator>
BitmapSlab<ObjSize, Allocator>::BitmapSlab(std::size_t capacity)
    : slab_(nullptr),
      bitmap_(nullptr),
      capacity_(capacity),
      internal_cap_((capacity + kBitmapUlongBits - 1) / kBitmapUlongBits * kBitmapUlongBits),
      num_words_(internal_cap_ / kBitmapUlongBits),
      free_count_(capacity) {
  // Allocate slab memory. Alignment must be a power of 2;
  // use at least alignof(std::max_align_t).
  constexpr std::size_t kSlabAlign =
      ObjSize >= alignof(std::max_align_t) &&
      (ObjSize & (ObjSize - 1)) == 0
          ? ObjSize
          : alignof(std::max_align_t);
  void* raw = allocator_.allocate(internal_cap_ * ObjSize, kSlabAlign);
  slab_ = static_cast<uint8_t*>(raw);

  // Allocate bitmap — all bits set to 1 (free).
  void* braw = allocator_.allocate(num_words_ * sizeof(uint64_t),
                                   alignof(uint64_t));
  bitmap_ = static_cast<uint64_t*>(braw);

  // Set all bits to 1 (free) for the real capacity slots.
  std::memset(bitmap_, 0xFF, num_words_ * sizeof(uint64_t));

  // If we rounded up, mark the padding slots as used (0) so they
  // are never allocated.
  if (internal_cap_ > capacity) {
    std::size_t padding = internal_cap_ - capacity;
    // Clear the top 'padding' bits of the last word.
    uint64_t mask = (uint64_t{1} << (kBitmapUlongBits - padding)) - 1;
    bitmap_[num_words_ - 1] = mask;
  }
}

template <std::size_t ObjSize, typename Allocator>
BitmapSlab<ObjSize, Allocator>::~BitmapSlab() {
  constexpr std::size_t kSlabAlign =
      ObjSize >= alignof(std::max_align_t) &&
      (ObjSize & (ObjSize - 1)) == 0
          ? ObjSize
          : alignof(std::max_align_t);
  allocator_.deallocate(bitmap_, alignof(uint64_t));
  allocator_.deallocate(slab_, kSlabAlign);
}

template <std::size_t ObjSize, typename Allocator>
std::size_t BitmapSlab<ObjSize, Allocator>::SlotIndex(const void* ptr) const {
  auto offset = static_cast<std::size_t>(
      static_cast<const uint8_t*>(ptr) - slab_);
  return offset / ObjSize;
}

template <std::size_t ObjSize, typename Allocator>
void BitmapSlab<ObjSize, Allocator>::MarkUsed(std::size_t start,
                                               std::size_t n) {
  for (std::size_t i = start; i < start + n; ++i) {
    std::size_t word = i / kBitmapUlongBits;
    std::size_t bit = i % kBitmapUlongBits;
    bitmap_[word] &= ~(uint64_t{1} << bit);
  }
  free_count_ -= n;
}

template <std::size_t ObjSize, typename Allocator>
void BitmapSlab<ObjSize, Allocator>::MarkFree(std::size_t start,
                                               std::size_t n) {
  for (std::size_t i = start; i < start + n; ++i) {
    std::size_t word = i / kBitmapUlongBits;
    std::size_t bit = i % kBitmapUlongBits;
    bitmap_[word] |= (uint64_t{1} << bit);
  }
  free_count_ += n;
}

template <std::size_t ObjSize, typename Allocator>
std::size_t BitmapSlab<ObjSize, Allocator>::FindContiguousFree(
    std::size_t n) const {
  if (n == 0 || n > capacity_ || free_count_ < n) return capacity_;

  const std::size_t end = internal_cap_;
  std::size_t pos = 0;

  while (pos < end) {
    // Find the next free (1) bit starting at 'pos'.
    std::size_t run_start = bitmap_scan(bitmap_, true, pos, end);
    if (run_start >= end) break;

    // Find the next used (0) bit after run_start — that's the end of
    // this free run.
    std::size_t run_end = bitmap_scan(bitmap_, false, run_start, end);

    std::size_t run_len = run_end - run_start;
    if (run_len >= n) return run_start;

    // Not long enough — skip past this run.
    pos = run_end;
  }

  return capacity_;
}

template <std::size_t ObjSize, typename Allocator>
template <typename T>
Span<T> BitmapSlab<ObjSize, Allocator>::AllocateN(std::size_t n) {
  static_assert(sizeof(T) == ObjSize,
                "sizeof(T) must equal the slab's ObjSize parameter");

  if (n == 0) return {nullptr, 0};

  std::size_t start = FindContiguousFree(n);
  if (start >= capacity_) return {nullptr, 0};

  MarkUsed(start, n);
  T* ptr = reinterpret_cast<T*>(slab_ + start * ObjSize);
  return {ptr, n};
}

template <std::size_t ObjSize, typename Allocator>
template <typename T>
void BitmapSlab<ObjSize, Allocator>::DeallocateN(T* ptr, std::size_t n) {
  static_assert(sizeof(T) == ObjSize,
                "sizeof(T) must equal the slab's ObjSize parameter");
  assert(ptr != nullptr);
  assert(n > 0);

  std::size_t start = SlotIndex(ptr);
  assert(start + n <= capacity_);
  MarkFree(start, n);
}

template <std::size_t ObjSize, typename Allocator>
template <typename T>
T* BitmapSlab<ObjSize, Allocator>::Allocate() {
  auto span = AllocateN<T>(1);
  return span.data;
}

template <std::size_t ObjSize, typename Allocator>
template <typename T>
void BitmapSlab<ObjSize, Allocator>::Deallocate(T* ptr) {
  DeallocateN(ptr, 1);
}

}  // namespace bitmap_slab

#endif  // BITMAP_SLAB_BITMAP_SLAB_H_
