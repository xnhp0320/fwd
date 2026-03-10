// rxtx/fast_lookup_table.h
#ifndef RXTX_FAST_LOOKUP_TABLE_H_
#define RXTX_FAST_LOOKUP_TABLE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "boost/intrusive/list.hpp"

#include "rxtx/allocator.h"
#include "rxtx/list_slab.h"
#include "rxtx/lookup_entry.h"
#include "rxtx/packet_metadata.h"

namespace rxtx {

// Node for the LRU doubly-linked list, stored in a parallel array indexed
// by slab slot. Keeps LRU tracking out of the 64-byte LookupEntry layout.
struct LruNode {
  boost::intrusive::list_member_hook<> lru_hook;
  std::size_t slot_index;  // index into slab backing array
};

using LruList = boost::intrusive::list<
    LruNode,
    boost::intrusive::member_hook<
        LruNode, boost::intrusive::list_member_hook<>, &LruNode::lru_hook>>;

// High-performance flow lookup table backed by absl::flat_hash_set
// with pointer-based hashing into slab-allocated LookupEntry items.
//
// NOT thread-safe for concurrent Insert/Remove/Find/ForEach calls.
// However, modifiable_ is std::atomic<bool> because SetModifiable is
// called from the ControlPlane thread while Insert/Remove/ForEach run
// on the PMD thread.
//
// Lookup uses a stack-allocated probe: a temporary LookupEntry is
// constructed on the stack, key fields populated, and its address
// passed to find(). The custom hash/eq functors dereference the
// pointer, so they work identically for slab and stack pointers.
//
// Iterators follow standard absl::flat_hash_set invalidation semantics.
// The caller must not use invalidated iterators after standalone
// Insert or Remove calls. ForEach handles erasure safely via
// set_.erase() return value.
template <typename Allocator = StdAllocator>
class FastLookupTable {
 public:
  using Set = absl::flat_hash_set<LookupEntry*, LookupEntryHash, LookupEntryEq>;
  using Iterator = Set::iterator;

  explicit FastLookupTable(std::size_t capacity);

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

  // Remove a flow entry by pointer. Returns true if removed.
  // Returns false without side effects if modifiable_ is false.
  bool Remove(LookupEntry* entry);

  void SetModifiable(bool m) { modifiable_.store(m, std::memory_order_release); }
  bool IsModifiable() const { return modifiable_.load(std::memory_order_acquire); }

  Iterator Begin() { return set_.begin(); }
  Iterator End() { return set_.end(); }

  template <typename Fn>
  std::size_t ForEach(Iterator& it, std::size_t count, Fn fn);

  // Evict up to batch_size least-recently-used entries from the table.
  // Returns the number of entries actually removed.
  std::size_t EvictLru(std::size_t batch_size);

  std::size_t size() const { return set_.size(); }
  std::size_t capacity() const { return slab_.capacity(); }

 private:
  static void FillEntry(LookupEntry* entry,
                        const IpAddress& src_ip, const IpAddress& dst_ip,
                        uint16_t src_port, uint16_t dst_port,
                        uint8_t protocol, uint32_t vni, uint8_t flags);

  std::size_t SlotIndex(const LookupEntry* entry) const {
    return (reinterpret_cast<const uint8_t*>(entry) - slab_.slab_base())
           / sizeof(LookupEntry);
  }

  ListSlab<sizeof(LookupEntry), Allocator> slab_;
  Set set_;
  std::atomic<bool> modifiable_{true};
  std::unique_ptr<LruNode[]> lru_nodes_;
  LruList lru_list_;
};

// --- Implementation ---

template <typename Allocator>
FastLookupTable<Allocator>::FastLookupTable(std::size_t capacity)
    : slab_(capacity), lru_nodes_(std::make_unique<LruNode[]>(capacity)) {}

template <typename Allocator>
void FastLookupTable<Allocator>::FillEntry(
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
LookupEntry* FastLookupTable<Allocator>::Find(
    const IpAddress& src_ip, const IpAddress& dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, uint32_t vni, uint8_t flags) {
  LookupEntry probe{};
  FillEntry(&probe, src_ip, dst_ip, src_port, dst_port,
            protocol, vni, flags);
  auto it = set_.find(&probe);
  if (it == set_.end()) return nullptr;
  LookupEntry* entry = *it;
  std::size_t slot = SlotIndex(entry);
  lru_list_.splice(lru_list_.end(), lru_list_,
                   lru_list_.iterator_to(lru_nodes_[slot]));
  return entry;
}

template <typename Allocator>
LookupEntry* FastLookupTable<Allocator>::Find(
    const PacketMetadata& meta) {
  LookupEntry probe{};
  probe.FromMetadata(meta);
  auto it = set_.find(&probe);
  if (it == set_.end()) return nullptr;
  LookupEntry* entry = *it;
  std::size_t slot = SlotIndex(entry);
  lru_list_.splice(lru_list_.end(), lru_list_,
                   lru_list_.iterator_to(lru_nodes_[slot]));
  return entry;
}

template <typename Allocator>
LookupEntry* FastLookupTable<Allocator>::Insert(
    const IpAddress& src_ip, const IpAddress& dst_ip,
    uint16_t src_port, uint16_t dst_port,
    uint8_t protocol, uint32_t vni, uint8_t flags) {
  if (!modifiable_.load(std::memory_order_acquire)) return nullptr;

  LookupEntry probe{};
  FillEntry(&probe, src_ip, dst_ip, src_port, dst_port,
            protocol, vni, flags);
  auto it = set_.find(&probe);
  if (it != set_.end()) return *it;

  LookupEntry* entry = slab_.template Allocate<LookupEntry>();
  if (entry == nullptr) return nullptr;
  FillEntry(entry, src_ip, dst_ip, src_port, dst_port,
            protocol, vni, flags);
  set_.insert(entry);
  std::size_t slot = SlotIndex(entry);
  lru_nodes_[slot].slot_index = slot;
  lru_list_.push_back(lru_nodes_[slot]);
  return entry;
}

template <typename Allocator>
bool FastLookupTable<Allocator>::Remove(LookupEntry* entry) {
  if (!modifiable_.load(std::memory_order_acquire)) return false;

  auto erased = set_.erase(entry);
  if (erased == 0) return false;
  std::size_t slot = SlotIndex(entry);
  lru_list_.erase(lru_list_.iterator_to(lru_nodes_[slot]));
  slab_.template Deallocate<LookupEntry>(entry);
  return true;
}

template <typename Allocator>
template <typename Fn>
std::size_t FastLookupTable<Allocator>::ForEach(
    Iterator& it, std::size_t count, Fn fn) {
  std::size_t visited = 0;
  while (visited < count && it != set_.end()) {
    LookupEntry* entry = *it;
    // Advance iterator before potential erase, since
    // absl::flat_hash_set::erase(iterator) returns void.
    auto current = it;
    ++it;
    if (fn(entry)) {
      set_.erase(current);
      std::size_t slot = SlotIndex(entry);
      lru_list_.erase(lru_list_.iterator_to(lru_nodes_[slot]));
      slab_.template Deallocate<LookupEntry>(entry);
    }
    ++visited;
  }
  return visited;
}

template <typename Allocator>
std::size_t FastLookupTable<Allocator>::EvictLru(std::size_t batch_size) {
  std::size_t removed = 0;
  while (removed < batch_size && !lru_list_.empty()) {
    LruNode& node = lru_list_.front();
    lru_list_.pop_front();
    LookupEntry* entry = reinterpret_cast<LookupEntry*>(
        const_cast<uint8_t*>(slab_.slab_base()) +
        node.slot_index * sizeof(LookupEntry));
    set_.erase(entry);
    slab_.template Deallocate<LookupEntry>(entry);
    ++removed;
  }
  return removed;
}

}  // namespace rxtx

#endif  // RXTX_FAST_LOOKUP_TABLE_H_
