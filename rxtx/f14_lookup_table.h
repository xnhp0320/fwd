// rxtx/f14_lookup_table.h
#ifndef RXTX_F14_LOOKUP_TABLE_H_
#define RXTX_F14_LOOKUP_TABLE_H_

// F14-backed flow lookup table — drop-in alternative to FastLookupTable.
//
// Composes F14Map (with pointer key/value, set semantics) with ListSlab
// for entry allocation, a parallel LruNode array for O(1) LRU tracking,
// and a std::atomic<bool> modifiable flag for cross-thread control.
//
// NOT thread-safe for concurrent Insert/Remove/Find/ForEach calls.
// SetModifiable is called from the ControlPlane thread while data-plane
// operations run on the PMD thread.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "boost/intrusive/list.hpp"

#include "rxtx/allocator.h"
#include "rxtx/f14_map.h"
#include "rxtx/list_slab.h"
#include "rxtx/lookup_entry.h"
#include "rxtx/packet_metadata.h"

namespace rxtx {

// LRU node for F14LookupTable, stored in a parallel array indexed by slab
// slot. Keeps LRU tracking out of the 64-byte LookupEntry layout.
// Defined independently from FastLookupTable's LruNode to avoid pulling
// in absl dependencies.
struct F14LruNode {
  boost::intrusive::list_member_hook<> lru_hook;
  std::size_t slot_index;  // index into slab backing array
};

using F14LruList = boost::intrusive::list<
    F14LruNode,
    boost::intrusive::member_hook<
        F14LruNode, boost::intrusive::list_member_hook<>,
        &F14LruNode::lru_hook>>;

template <typename Allocator = StdAllocator>
class F14LookupTable {
 public:
  using PrefetchContext = f14::HashPair;

  explicit F14LookupTable(std::size_t capacity);

  // Insert a flow entry. Returns pointer to the entry (existing or new).
  // Returns nullptr if the slab is full and the key doesn't already exist.
  // Returns nullptr without side effects if modifiable_ is false.
  LookupEntry* Insert(const IpAddress& src_ip, const IpAddress& dst_ip,
                       uint16_t src_port, uint16_t dst_port,
                       uint8_t protocol, uint32_t vni, uint8_t flags);

  // Find a flow entry. Returns pointer if found, nullptr otherwise.
  // Non-const because a hit promotes the entry in the LRU list.
  LookupEntry* Find(const IpAddress& src_ip, const IpAddress& dst_ip,
                     uint16_t src_port, uint16_t dst_port,
                     uint8_t protocol, uint32_t vni, uint8_t flags);

  // Convenience: find using PacketMetadata directly.
  // Non-const because a hit promotes the entry in the LRU list.
  LookupEntry* Find(const PacketMetadata& meta);

  // Build prefetch context and prefetch target chunk for subsequent lookup.
  void Prefetch(const PacketMetadata& meta, PrefetchContext& ctx);

  // Find using precomputed prefetch context.
  LookupEntry* FindWithPrefetch(const PacketMetadata& meta,
                                const PrefetchContext& ctx);

  // Remove a flow entry by pointer. Returns true if removed.
  // Returns false without side effects if modifiable_ is false.
  bool Remove(LookupEntry* entry);

  void SetModifiable(bool m) { modifiable_.store(m, std::memory_order_release); }
  bool IsModifiable() const { return modifiable_.load(std::memory_order_acquire); }

  // Visit up to count entries in LRU order (oldest to newest).
  // fn(LookupEntry*) returns true to erase the entry.
  // Returns the number of entries visited.
  template <typename Fn>
  std::size_t ForEach(std::size_t count, Fn fn);

  // Evict up to batch_size least-recently-used entries from the table.
  // Returns the number of entries actually removed.
  std::size_t EvictLru(std::size_t batch_size);

  // Visit all entries in LRU order (oldest to newest).
  // fn(LookupEntry*) is called for each entry. No erasure support.
  template <typename Fn>
  void ForEachEntry(Fn fn) {
    for (auto it = lru_list_.begin(); it != lru_list_.end(); ++it) {
      F14LruNode& node = *it;
      LookupEntry* entry = reinterpret_cast<LookupEntry*>(
          const_cast<uint8_t*>(slab_.slab_base()) +
          node.slot_index * sizeof(LookupEntry));
      fn(entry);
    }
  }

  std::size_t size() const { return map_.size(); }
  std::size_t capacity() const { return slab_.capacity(); }

 private:
  // F14Map with pointer key/value (set semantics), item iteration disabled
  // since F14LookupTable iterates via the LRU list.
  using Map = f14::F14Map<LookupEntry*, LookupEntry*,
                          LookupEntryHash, LookupEntryEq,
                          f14::DefaultChunkAllocator,
                          /*EnableItemIteration=*/false>;

  static void FillEntry(LookupEntry* entry,
                        const IpAddress& src_ip, const IpAddress& dst_ip,
                        uint16_t src_port, uint16_t dst_port,
                        uint8_t protocol, uint32_t vni, uint8_t flags);

  std::size_t SlotIndex(const LookupEntry* entry) const {
    return (reinterpret_cast<const uint8_t*>(entry) - slab_.slab_base())
           / sizeof(LookupEntry);
  }

  ListSlab<sizeof(LookupEntry), Allocator> slab_;
  Map map_;
  std::atomic<bool> modifiable_{true};
  std::unique_ptr<F14LruNode[]> lru_nodes_;
  F14LruList lru_list_;
};

// ===========================================================================
// Implementation
// ===========================================================================

template <typename Allocator>
F14LookupTable<Allocator>::F14LookupTable(std::size_t capacity)
    : slab_(capacity), lru_nodes_(std::make_unique<F14LruNode[]>(capacity)) {}

template <typename Allocator>
void F14LookupTable<Allocator>::FillEntry(
    LookupEntry* entry, const IpAddress& src_ip, const IpAddress& dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, uint32_t vni, uint8_t flags) {
  entry->src_ip = src_ip;
  entry->dst_ip = dst_ip;
  entry->src_port = src_port;
  entry->dst_port = dst_port;
  entry->protocol = protocol;
  entry->vni = vni;
  entry->flags = flags;
}

template <typename Allocator>
LookupEntry* F14LookupTable<Allocator>::Find(
    const IpAddress& src_ip, const IpAddress& dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, uint32_t vni, uint8_t flags) {
  LookupEntry probe{};
  FillEntry(&probe, src_ip, dst_ip, src_port, dst_port,
            protocol, vni, flags);
  LookupEntry** val = map_.Find(&probe);
  if (val == nullptr) return nullptr;
  LookupEntry* entry = *val;
  std::size_t slot = SlotIndex(entry);
  lru_list_.splice(lru_list_.end(), lru_list_,
                   lru_list_.iterator_to(lru_nodes_[slot]));
  return entry;
}

template <typename Allocator>
LookupEntry* F14LookupTable<Allocator>::Find(
    const PacketMetadata& meta) {
  PrefetchContext ctx{};
  Prefetch(meta, ctx);
  return FindWithPrefetch(meta, ctx);
}

template <typename Allocator>
void F14LookupTable<Allocator>::Prefetch(const PacketMetadata& meta,
                                         PrefetchContext& ctx) {
  LookupEntry probe{};
  probe.FromMetadata(meta);
  map_.Prefetch(&probe, ctx);
}

template <typename Allocator>
LookupEntry* F14LookupTable<Allocator>::FindWithPrefetch(
    const PacketMetadata& meta, const PrefetchContext& ctx) {
  LookupEntry probe{};
  probe.FromMetadata(meta);
  LookupEntry** val = map_.FindWithHashPair(&probe, ctx);
  if (val == nullptr) return nullptr;
  LookupEntry* entry = *val;
  std::size_t slot = SlotIndex(entry);
  lru_list_.splice(lru_list_.end(), lru_list_,
                   lru_list_.iterator_to(lru_nodes_[slot]));
  return entry;
}

template <typename Allocator>
LookupEntry* F14LookupTable<Allocator>::Insert(
    const IpAddress& src_ip, const IpAddress& dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, uint32_t vni, uint8_t flags) {
  if (!modifiable_.load(std::memory_order_acquire)) return nullptr;

  // Probe for existing entry
  LookupEntry probe{};
  FillEntry(&probe, src_ip, dst_ip, src_port, dst_port,
            protocol, vni, flags);
  LookupEntry** existing = map_.Find(&probe);
  if (existing != nullptr) return *existing;

  // Allocate from slab
  LookupEntry* entry = slab_.template Allocate<LookupEntry>();
  if (entry == nullptr) return nullptr;

  FillEntry(entry, src_ip, dst_ip, src_port, dst_port,
            protocol, vni, flags);

  // Insert pointer into F14Map
  map_.Insert(entry, entry);

  // Add to LRU tail
  std::size_t slot = SlotIndex(entry);
  lru_nodes_[slot].slot_index = slot;
  lru_list_.push_back(lru_nodes_[slot]);
  return entry;
}

template <typename Allocator>
bool F14LookupTable<Allocator>::Remove(LookupEntry* entry) {
  if (!modifiable_.load(std::memory_order_acquire)) return false;

  bool erased = map_.Erase(entry);
  if (!erased) return false;

  std::size_t slot = SlotIndex(entry);
  lru_list_.erase(lru_list_.iterator_to(lru_nodes_[slot]));
  slab_.template Deallocate<LookupEntry>(entry);
  return true;
}

template <typename Allocator>
template <typename Fn>
std::size_t F14LookupTable<Allocator>::ForEach(std::size_t count, Fn fn) {
  std::size_t visited = 0;
  auto it = lru_list_.begin();
  while (visited < count && it != lru_list_.end()) {
    F14LruNode& node = *it;
    LookupEntry* entry = reinterpret_cast<LookupEntry*>(
        const_cast<uint8_t*>(slab_.slab_base()) +
        node.slot_index * sizeof(LookupEntry));
    // Advance iterator before potential erase
    ++it;
    if (fn(entry)) {
      map_.Erase(entry);
      lru_list_.erase(lru_list_.iterator_to(node));
      slab_.template Deallocate<LookupEntry>(entry);
    }
    ++visited;
  }
  return visited;
}

template <typename Allocator>
std::size_t F14LookupTable<Allocator>::EvictLru(std::size_t batch_size) {
  std::size_t removed = 0;
  while (removed < batch_size && !lru_list_.empty()) {
    F14LruNode& node = lru_list_.front();
    lru_list_.pop_front();
    LookupEntry* entry = reinterpret_cast<LookupEntry*>(
        const_cast<uint8_t*>(slab_.slab_base()) +
        node.slot_index * sizeof(LookupEntry));
    map_.Erase(entry);
    slab_.template Deallocate<LookupEntry>(entry);
    ++removed;
  }
  return removed;
}

}  // namespace rxtx

#endif  // RXTX_F14_LOOKUP_TABLE_H_
