#ifndef RXTX_F14_MAP_H_
#define RXTX_F14_MAP_H_

// F14 hash map — C++20 template port of the C fmap (processor/fmap.h).
//
// This file contains:
//   - ChunkHeader / Chunk<Item>: the 128-byte chunk data structure
//   - HashPair / SplitHash / ProbeDelta: hash splitting utilities
//   - PackedPtr / PackedFromItemPtr: packed pointer encoding
//   - DefaultChunkAllocator: aligned chunk memory allocator
//   - F14Map<Key, Value, Hash, KeyEqual, Allocator, EnableItemIteration>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "rxtx/f14_simd.h"

namespace rxtx::f14 {

// ===========================================================================
// ChunkHeader — 16-byte SIMD-aligned header for each chunk.
//
// Layout (matches C fmap's struct fmap_chunk_head):
//   bytes  0–13: tags[14]  — one-byte hash fingerprints (0 = empty)
//   byte     14: control   — low nibble: scale, high nibble: hosted overflow
//   byte     15: overflow  — overflow reference count (saturates at 255)
// ===========================================================================

struct alignas(16) ChunkHeader {
  uint8_t tags[14];
  uint8_t control;   // low nibble: scale, high nibble: hosted overflow count
  uint8_t overflow;  // overflow reference count (saturates at 255)
};

static_assert(sizeof(ChunkHeader) == 16);
static_assert(alignof(ChunkHeader) == 16);

// ===========================================================================
// Chunk<Item> — 128-byte aligned chunk: header + 14 item slots.
//
// For pointer items (e.g. void*, LookupEntry*), each slot is 8 bytes,
// giving 16 + 14×8 = 128 bytes = 2 cache lines, matching the C fmap.
// ===========================================================================

template <typename Item>
struct alignas(128) Chunk {
  ChunkHeader header;
  Item items[14];

  // --- Tag access ---
  uint8_t GetTag(int idx) const { return header.tags[idx]; }
  void SetTag(int idx, uint8_t tag) { header.tags[idx] = tag; }
  void ClearTag(int idx) { header.tags[idx] = 0; }
  bool SlotUsed(int idx) const { return header.tags[idx] != 0; }

  // --- Overflow counting (saturates at 255) ---
  uint8_t OverflowCount() const { return header.overflow; }
  void IncOverflow() {
    if (header.overflow != 255) ++header.overflow;
  }
  void DecOverflow() {
    if (header.overflow != 255) --header.overflow;
  }

  // --- Hosted overflow (high nibble of control) ---
  uint8_t HostedOverflowCount() const { return header.control >> 4; }
  void AdjHostedOverflow(int8_t delta) {
    header.control += static_cast<uint8_t>(delta << 4);
  }

  // --- Scale (low nibble of control, only meaningful on chunk 0) ---
  uint8_t Scale() const { return header.control & 0x0F; }
  void SetScale(uint8_t scale) {
    header.control = (header.control & 0xF0) | (scale & 0x0F);
  }

  // --- Clear all tags and control ---
  void Clear() { std::memset(&header, 0, sizeof(header)); }

  // --- SIMD operations ---
  TagMask OccupiedMask() const {
    return SimdBackend::OccupiedMask(&header);
  }
  TagMask TagMatch(uint8_t needle) const {
    return SimdBackend::TagMatch(&header, needle);
  }

  // --- First empty slot index, or -1 if full ---
  int FirstEmpty() const {
    TagMask empty = OccupiedMask() ^ kFullMask;
    return empty ? __builtin_ctz(empty) : -1;
  }
};

// Verify layout matches C fmap: Chunk<void*> = 16 + 14×8 = 128 bytes.
static_assert(sizeof(Chunk<void*>) == 128);

// ===========================================================================
// DefaultChunkAllocator — aligned allocation for chunk arrays.
// ===========================================================================

struct DefaultChunkAllocator {
  void* allocate(std::size_t bytes, std::size_t alignment) {
    return ::operator new(bytes, std::align_val_t{alignment});
  }
  void deallocate(void* ptr) {
    ::operator delete(ptr, std::align_val_t{128});
  }
};

// ===========================================================================
// HashPair / SplitHash / ProbeDelta — hash splitting utilities.
//
// SplitHash: tag = (hash >> 24) | 0x80  (always non-zero, bit 7 set)
// ProbeDelta: delta = 2*tag + 1         (always odd → coprime with 2^n)
// ===========================================================================

struct HashPair {
  std::size_t hash;
  uint8_t tag;
};

inline HashPair SplitHash(std::size_t hash) {
  return {hash, static_cast<uint8_t>((hash >> 24) | 0x80)};
}

inline std::size_t ProbeDelta(uint8_t tag) {
  return 2 * static_cast<std::size_t>(tag) + 1;
}

// ===========================================================================
// PackedPtr / PackedFromItemPtr — packed pointer encoding.
//
// Matches the C fmap's fmap_packed_ptr / fmap_item_iter_packed encoding:
//   encoded = index >> 1
//   raw     = item_ptr | encoded
//
// The low bits of the item pointer are available because items are aligned
// within the 128-byte chunk structure.
// ===========================================================================

struct PackedPtr {
  std::uintptr_t raw = 0;

  bool operator==(const PackedPtr& o) const { return raw == o.raw; }
  bool operator!=(const PackedPtr& o) const { return raw != o.raw; }
  bool operator<(const PackedPtr& o) const { return raw < o.raw; }
};

inline PackedPtr PackedFromItemPtr(void* item_ptr, std::size_t index) {
  std::uintptr_t encoded = index >> 1;
  std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(item_ptr) | encoded;
  return PackedPtr{raw};
}

// ===========================================================================
// F14Map — C++ template F14 hash map.
//
// Ports the C fmap algorithm (processor/fmap.c) to a modern C++20 template
// with compile-time hash/equality/allocation inlining.
// ===========================================================================

template <typename Key, typename Value,
          typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>,
          typename Allocator = DefaultChunkAllocator,
          bool EnableItemIteration = true>
class F14Map {
 public:
  // Set mode: when Key == Value, store Key directly (no pair overhead).
  // Map mode: when Key != Value, store std::pair<Key, Value>.
  static constexpr bool kIsSetMode = std::is_same_v<Key, Value>;
  using Item = std::conditional_t<kIsSetMode, Key, std::pair<Key, Value>>;
  using Mapped = std::conditional_t<kIsSetMode, Key, Value>;
  using ChunkType = Chunk<Item>;

  // Extract the key from a stored item.
  static const Key& ItemKey(const Item& item) {
    if constexpr (kIsSetMode) return item;
    else return item.first;
  }
  // Extract the mapped value from a stored item.
  static Mapped& ItemMapped(Item& item) {
    if constexpr (kIsSetMode) return item;
    else return item.second;
  }
  static const Mapped& ItemMapped(const Item& item) {
    if constexpr (kIsSetMode) return item;
    else return item.second;
  }

  // --- ItemIterator (stub — full implementation in Task 2.3) ---
  class ItemIterator {
   public:
    ItemIterator() : item_ptr_(nullptr), index_(0) {}
    ItemIterator(Item* item_ptr, std::size_t index)
        : item_ptr_(item_ptr), index_(index) {}

    bool AtEnd() const { return item_ptr_ == nullptr; }
    Item& operator*() const { return *item_ptr_; }
    Item* operator->() const { return item_ptr_; }

    void Advance() { AdvanceImpl(true, false); }
    void AdvancePrechecked() { AdvanceImpl(false, false); }
    void AdvanceLikelyDead() { AdvanceImpl(true, true); }

    bool operator==(const ItemIterator& o) const {
      return item_ptr_ == o.item_ptr_;
    }
    bool operator!=(const ItemIterator& o) const {
      return item_ptr_ != o.item_ptr_;
    }

   private:
    void AdvanceImpl(bool check_eof, bool likely_dead) {
      ChunkType* chunk = ToChunk();

      // Walk backward through items in the current chunk
      while (index_ > 0) {
        --index_;
        --item_ptr_;
        if (chunk->SlotUsed(static_cast<int>(index_))) [[likely]] {
          return;
        }
      }

      // Move to previous chunks. The for-loop pattern (rather than
      // while(true)) helps the compiler eliminate dead code when
      // likely_dead is a compile-time constant true.
      for (std::size_t i = 1; !likely_dead || i != 0; ++i) {
        if (check_eof) {
          // EOF = chunk 0, which has scale != 0 in the control byte
          if (chunk->Scale() != 0) [[unlikely]] {
            item_ptr_ = nullptr;
            return;
          }
        }
        --chunk;

        if (check_eof && !likely_dead) {
          __builtin_prefetch(chunk - 1, 0, 1);
        }

        // Find the last occupied slot in this chunk
        TagMask occ = chunk->OccupiedMask();
        if (occ == 0) continue;

        // find_last_set(mask) - 1: highest set bit position
        int last_idx = 31 - __builtin_clz(occ);
        item_ptr_ = &chunk->items[last_idx];
        index_ = static_cast<std::size_t>(last_idx);
        return;
      }
    }

    ChunkType* ToChunk() const {
      auto addr = reinterpret_cast<std::uintptr_t>(item_ptr_);
      addr -= index_ * sizeof(Item);
      addr -= offsetof(ChunkType, items);
      return reinterpret_cast<ChunkType*>(addr);
    }

    Item* item_ptr_;
    std::size_t index_;

    friend class F14Map;
  };

  explicit F14Map(std::size_t init_capacity = 0)
      : chunks_(nullptr), chunk_mask_(0), size_(0) {
    if constexpr (EnableItemIteration) {
      // PackedPtr encoding steals the low 3 bits of the item pointer,
      // which requires items to be at least 8-byte aligned within chunks.
      static_assert(sizeof(Item) >= 8,
                    "EnableItemIteration requires sizeof(Item) >= 8 "
                    "for packed pointer encoding");
      packed_storage_.packed_begin_ = PackedPtr{0};
    }
    if (init_capacity > 0) {
      std::size_t new_chunk_count = 0;
      std::size_t new_scale = 0;
      Compute(init_capacity, &new_chunk_count, &new_scale);
      Rehash(new_chunk_count, new_scale);
    }
  }

  ~F14Map() {
    if (chunks_ != nullptr) {
      // Destroy all items
      std::size_t chunk_count = chunk_mask_ + 1;
      for (std::size_t ci = 0; ci < chunk_count; ++ci) {
        TagMask occ = chunks_[ci].OccupiedMask();
        while (occ) {
          int idx = __builtin_ctz(occ);
          chunks_[ci].items[idx].~Item();
          occ &= occ - 1;
        }
      }
      allocator_.deallocate(chunks_);
    }
  }

  // Non-copyable, non-movable for now.
  F14Map(const F14Map&) = delete;
  F14Map& operator=(const F14Map&) = delete;

  void Prefetch(const Key& key, HashPair& hp_out) {
    hp_out = SplitHash(hash_(key));
    if (chunks_ == nullptr) return;
    ChunkType& chunk = chunks_[hp_out.hash & chunk_mask_];
    __builtin_prefetch(&chunk, 0, 1);
  }

  Mapped* FindWithHashPair(const Key& key, const HashPair& hp) {
    if (chunks_ == nullptr) return nullptr;
    std::size_t index = hp.hash;
    std::size_t delta = ProbeDelta(hp.tag);

    for (std::size_t tries = 0; tries <= chunk_mask_; ++tries) {
      ChunkType& chunk = chunks_[index & chunk_mask_];
      __builtin_prefetch(&chunk.items[6], 0, 1);

      TagMask matches = chunk.TagMatch(hp.tag);
      while (matches) {
        int i = __builtin_ctz(matches);
        if (key_eq_(key, ItemKey(chunk.items[i]))) [[likely]] {
          return &ItemMapped(chunk.items[i]);
        }
        matches &= matches - 1;
      }

      if (chunk.OverflowCount() == 0) [[likely]] {
        break;
      }
      index += delta;
    }
    return nullptr;
  }

  const Mapped* FindWithHashPair(const Key& key, const HashPair& hp) const {
    return const_cast<F14Map*>(this)->FindWithHashPair(key, hp);
  }

  Mapped* Find(const Key& key) {
    HashPair hp = SplitHash(hash_(key));
    return FindWithHashPair(key, hp);
  }

  const Mapped* Find(const Key& key) const {
    return const_cast<F14Map*>(this)->Find(key);
  }

  std::pair<Mapped*, bool> Insert(const Key& key, const Value& value) {
    // Check if key already exists
    if (size_ > 0) {
      Mapped* existing = Find(key);
      if (existing != nullptr) {
        return {existing, false};
      }
    }

    ReserveForInsert(1);
    if (chunks_ == nullptr) [[unlikely]] {
      // Allocation failed
      return {nullptr, false};
    }

    auto hp = SplitHash(hash_(key));
    std::size_t index = hp.hash;
    ChunkType* chunk = &chunks_[index & chunk_mask_];
    int empty_idx = chunk->FirstEmpty();

    if (empty_idx == -1) {
      std::size_t delta = ProbeDelta(hp.tag);
      do {
        chunk->IncOverflow();
        index += delta;
        chunk = &chunks_[index & chunk_mask_];
        empty_idx = chunk->FirstEmpty();
      } while (empty_idx == -1);
      chunk->AdjHostedOverflow(1);
    }

    chunk->SetTag(empty_idx, hp.tag);
    if constexpr (kIsSetMode) {
      new (&chunk->items[empty_idx]) Item(key);
    } else {
      new (&chunk->items[empty_idx]) Item(key, value);
    }

    if constexpr (EnableItemIteration) {
      AdjPackedBeginAfterInsert(&chunk->items[empty_idx],
                                static_cast<std::size_t>(empty_idx));
    }

    ++size_;
    return {&ItemMapped(chunk->items[empty_idx]), true};
  }

  bool Erase(const Key& key) {
    if (chunks_ == nullptr || size_ == 0) return false;

    auto hp = SplitHash(hash_(key));
    std::size_t index = hp.hash;
    std::size_t delta = ProbeDelta(hp.tag);

    for (std::size_t tries = 0; tries <= chunk_mask_; ++tries) {
      ChunkType& chunk = chunks_[index & chunk_mask_];

      TagMask matches = chunk.TagMatch(hp.tag);
      while (matches) {
        int i = __builtin_ctz(matches);
        if (key_eq_(key, ItemKey(chunk.items[i]))) [[likely]] {
          // Found the item to erase
          if constexpr (EnableItemIteration) {
            AdjPackedBeginBeforeErase(
                ItemIterator(&chunk.items[i], static_cast<std::size_t>(i)));
          }

          chunk.ClearTag(i);
          chunk.items[i].~Item();

          // Walk probe chain to fix overflow counts
          if (chunk.HostedOverflowCount() > 0) {
            std::size_t probe_idx = hp.hash;
            uint8_t hostedOp = 0;
            while (true) {
              ChunkType* probe_chunk =
                  &chunks_[probe_idx & chunk_mask_];
              if (probe_chunk == &chunk) {
                if (hostedOp != 0) {
                  probe_chunk->AdjHostedOverflow(-1);
                }
                break;
              }
              probe_chunk->DecOverflow();
              hostedOp = 1;
              probe_idx += delta;
            }
          }

          --size_;
          return true;
        }
        matches &= matches - 1;
      }

      if (chunk.OverflowCount() == 0) [[likely]] {
        break;
      }
      index += delta;
    }
    return false;
  }

  std::size_t size() const { return size_; }

  void Clear() {
    if (chunks_ == nullptr) return;

    std::size_t chunk_count = chunk_mask_ + 1;

    if (chunk_count >= 16 || size_ == 0) {
      // Reset: deallocate and go back to empty state
      for (std::size_t ci = 0; ci < chunk_count; ++ci) {
        TagMask occ = chunks_[ci].OccupiedMask();
        while (occ) {
          int idx = __builtin_ctz(occ);
          chunks_[ci].items[idx].~Item();
          occ &= occ - 1;
        }
      }
      allocator_.deallocate(chunks_);
      chunks_ = nullptr;
      chunk_mask_ = 0;
      size_ = 0;
      if constexpr (EnableItemIteration) {
        packed_storage_.packed_begin_ = PackedPtr{0};
      }
      return;
    }

    // Small table: clear in-place
    uint8_t scale = chunks_[0].Scale();
    for (std::size_t ci = 0; ci < chunk_count; ++ci) {
      TagMask occ = chunks_[ci].OccupiedMask();
      while (occ) {
        int idx = __builtin_ctz(occ);
        chunks_[ci].items[idx].~Item();
        occ &= occ - 1;
      }
      chunks_[ci].Clear();
    }
    chunks_[0].SetScale(scale);
    size_ = 0;
    if constexpr (EnableItemIteration) {
      packed_storage_.packed_begin_ = PackedPtr{0};
    }
  }

  template <typename Fn>
  void ForEach(Fn fn) {
    if (chunks_ == nullptr) return;
    std::size_t chunk_count = chunk_mask_ + 1;
    for (std::size_t ci = 0; ci < chunk_count; ++ci) {
      TagMask occ = chunks_[ci].OccupiedMask();
      while (occ) {
        int idx = __builtin_ctz(occ);
        occ &= occ - 1;
        Item& item = chunks_[ci].items[idx];
        bool should_erase = fn(ItemKey(item), ItemMapped(item));
        if (should_erase) {
          if constexpr (EnableItemIteration) {
            AdjPackedBeginBeforeErase(
                ItemIterator(&item, static_cast<std::size_t>(idx)));
          }

          auto hp = SplitHash(hash_(ItemKey(item)));
          chunks_[ci].ClearTag(idx);

          // Fix overflow counts
          if (chunks_[ci].HostedOverflowCount() > 0) {
            std::size_t probe_idx = hp.hash;
            std::size_t delta = ProbeDelta(hp.tag);
            uint8_t hostedOp = 0;
            while (true) {
              ChunkType* probe_chunk =
                  &chunks_[probe_idx & chunk_mask_];
              if (probe_chunk == &chunks_[ci]) {
                if (hostedOp != 0) {
                  probe_chunk->AdjHostedOverflow(-1);
                }
                break;
              }
              probe_chunk->DecOverflow();
              hostedOp = 1;
              probe_idx += delta;
            }
          }

          item.~Item();
          --size_;
        }
      }
    }
  }

  // --- Item iteration ---
  ItemIterator Begin() const {
    if constexpr (EnableItemIteration) {
      if (size_ == 0) return ItemIterator{};

      // Decode packed_begin_ back to an ItemIterator.
      // Matches C fmap's fmap_item_iter_from_packed:
      //   encoded = (raw & 0x7) << 1
      //   deduced = ((raw >> 3) * 1) & 0x1  (i.e., bit 3 of raw)
      //   index   = encoded | deduced
      //   item_ptr = raw & ~0x7
      std::uintptr_t raw = packed_storage_.packed_begin_.raw;
      std::uintptr_t encoded = (raw & 0x7) << 1;
      std::uintptr_t deduced = (raw >> 3) & 0x1;
      std::size_t index = static_cast<std::size_t>(encoded | deduced);
      Item* item_ptr = reinterpret_cast<Item*>(raw & ~std::uintptr_t{0x7});
      return ItemIterator{item_ptr, index};
    } else {
      return ItemIterator{};
    }
  }

  ItemIterator End() const { return ItemIterator{}; }

  // --- Accessors for testing ---
  ChunkType* chunks() const { return chunks_; }
  std::size_t chunk_mask() const { return chunk_mask_; }

 private:
  static void Compute(std::size_t desired,
                      std::size_t* chunk_count,
                      std::size_t* scale) {
    std::size_t min_chunks = (desired - 1) / kDesiredCapacity + 1;
    // find_last_set: returns 1 + floor(log2(x)) for x > 0, 0 for x == 0
    std::size_t chunk_pow = 0;
    if (min_chunks > 1) {
      chunk_pow = FindLastSet(static_cast<uint32_t>(min_chunks - 1));
    }
    *chunk_count = std::size_t{1} << chunk_pow;
    *scale = kDesiredCapacity;
  }

  static std::size_t FindLastSet(uint32_t mask) {
    return mask ? 1 + (31 ^ __builtin_clz(mask)) : 0;
  }

  static std::size_t ComputeCapacity(std::size_t chunk_count,
                                     std::size_t scale) {
    return chunk_count * scale;
  }

  void InitChunks(ChunkType* chunks, std::size_t chunk_count,
                  std::size_t scale) {
    for (std::size_t i = 0; i < chunk_count; ++i) {
      chunks[i].Clear();
    }
    chunks[0].SetScale(static_cast<uint8_t>(scale));
  }

  void ReserveForInsert(std::size_t incoming) {
    std::size_t needed = size_ + incoming;

    if (chunks_ == nullptr) {
      // First allocation
      std::size_t new_chunk_count = 0;
      std::size_t new_scale = 0;
      Compute(needed, &new_chunk_count, &new_scale);
      Rehash(new_chunk_count, new_scale);
      return;
    }

    std::size_t chunk_count = chunk_mask_ + 1;
    std::size_t scale = chunks_[0].Scale();
    std::size_t existing = ComputeCapacity(chunk_count, scale);

    if (needed - 1 >= existing) {
      // Growth: ~1.41× factor
      std::size_t min_growth = existing + (existing >> 2)
                               + (existing >> 3) + (existing >> 5);
      std::size_t capacity = needed > min_growth ? needed : min_growth;

      std::size_t new_chunk_count = 0;
      std::size_t new_scale = 0;
      Compute(capacity, &new_chunk_count, &new_scale);
      Rehash(new_chunk_count, new_scale);
    }
  }

  void Rehash(std::size_t new_chunk_count, std::size_t new_scale) {
    std::size_t alloc_size = sizeof(ChunkType) * new_chunk_count;
    void* raw = allocator_.allocate(alloc_size, alignof(ChunkType));
    if (raw == nullptr) [[unlikely]] {
      return;  // Allocation failed — map remains valid
    }

    auto* new_chunks = static_cast<ChunkType*>(raw);
    InitChunks(new_chunks, new_chunk_count, new_scale);

    ChunkType* old_chunks = chunks_;
    std::size_t old_chunk_count = (chunks_ != nullptr) ? chunk_mask_ + 1 : 0;
    std::size_t old_scale =
        (old_chunks != nullptr) ? old_chunks[0].Scale() : 0;
    std::size_t old_capacity = ComputeCapacity(old_chunk_count, old_scale);

    // Switch to new chunks
    chunks_ = new_chunks;
    chunk_mask_ = new_chunk_count - 1;

    if (size_ == 0) {
      if (old_capacity > 0) {
        allocator_.deallocate(old_chunks);
      }
      return;
    }

    // Use stack-based fullness array for tracking fill during rehash
    uint8_t stack_buf[256];
    std::memset(stack_buf, 0, sizeof(stack_buf));
    uint8_t* fullness = stack_buf;
    bool heap_allocated = false;

    if (new_chunk_count > 256) {
      fullness = static_cast<uint8_t*>(
          ::operator new(new_chunk_count, std::nothrow));
      if (fullness == nullptr) [[unlikely]] {
        // Revert
        chunks_ = old_chunks;
        chunk_mask_ = old_chunk_count - 1;
        allocator_.deallocate(new_chunks);
        return;
      }
      std::memset(fullness, 0, new_chunk_count);
      heap_allocated = true;
    }

    // Re-insert all items from old chunks
    if constexpr (EnableItemIteration) {
      packed_storage_.packed_begin_ = PackedPtr{0};
    }

    for (std::size_t ci = old_chunk_count; ci > 0; --ci) {
      ChunkType& src_chunk = old_chunks[ci - 1];
      TagMask occ = src_chunk.OccupiedMask();
      while (occ) {
        int src_idx = __builtin_ctz(occ);
        occ &= occ - 1;

        Item& src_item = src_chunk.items[src_idx];
        auto hp = SplitHash(hash_(ItemKey(src_item)));

        // Allocate tag in new chunk array (fmap_alloc_tag logic)
        std::size_t idx = hp.hash;
        std::size_t delta = ProbeDelta(hp.tag);
        uint8_t hostedOp = 0;

        while (true) {
          std::size_t ci2 = idx & chunk_mask_;
          ChunkType& dst_chunk = chunks_[ci2];
          if (fullness[ci2] < kCapacity) [[likely]] {
            int dst_idx = fullness[ci2]++;
            dst_chunk.SetTag(dst_idx, hp.tag);
            if (hostedOp != 0) {
              dst_chunk.AdjHostedOverflow(1);
            }
            new (&dst_chunk.items[dst_idx])
                Item(std::move(src_item));
            src_item.~Item();

            if constexpr (EnableItemIteration) {
              AdjPackedBeginAfterInsert(&dst_chunk.items[dst_idx],
                                        static_cast<std::size_t>(dst_idx));
            }
            break;
          }
          chunks_[ci2].IncOverflow();
          hostedOp = 1;
          idx += delta;
        }
      }
    }

    if (heap_allocated) {
      ::operator delete(fullness);
    }

    if (old_capacity > 0) {
      allocator_.deallocate(old_chunks);
    }
  }

  void AdjPackedBeginAfterInsert(Item* item_ptr,
                                 std::size_t index) {
    PackedPtr ptr = PackedFromItemPtr(
        static_cast<void*>(item_ptr), index);
    if (ptr.raw > packed_storage_.packed_begin_.raw) {
      packed_storage_.packed_begin_ = ptr;
    }
  }

  void AdjPackedBeginBeforeErase(ItemIterator iter) {
    PackedPtr ptr = PackedFromItemPtr(
        static_cast<void*>(iter.item_ptr_), iter.index_);
    if (packed_storage_.packed_begin_.raw == ptr.raw) {
      if (size_ - 1 == 0) {
        packed_storage_.packed_begin_ = PackedPtr{0};
      } else {
        iter.AdvancePrechecked();
        packed_storage_.packed_begin_ = PackedFromItemPtr(
            static_cast<void*>(iter.item_ptr_), iter.index_);
      }
    }
  }

  ChunkType* chunks_;
  std::size_t chunk_mask_;  // chunk_count - 1
  std::size_t size_;
  Hash hash_;
  KeyEqual key_eq_;
  Allocator allocator_;

  struct PackedBeginStorage { PackedPtr packed_begin_; };
  struct EmptyStorage {};
  [[no_unique_address]]
  std::conditional_t<EnableItemIteration, PackedBeginStorage, EmptyStorage>
      packed_storage_;
};

}  // namespace rxtx::f14

#endif  // RXTX_F14_MAP_H_
