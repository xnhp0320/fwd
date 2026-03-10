// rxtx/list_slab.h
#ifndef RXTX_LIST_SLAB_H_
#define RXTX_LIST_SLAB_H_

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include <boost/intrusive/slist.hpp>

#include "rxtx/allocator.h"

namespace rxtx {

// Slab allocator parameterized by entry Size (bytes) to avoid code bloat.
// NOT thread-safe — must be accessed from a single thread.
//
// Typed access is through Allocate<T>() and Deallocate<T>(), which
// static_assert that sizeof(T) == Size and that T has a compatible
// slist_member_hook<> member named 'hook' at offset 0.
//
// Allocator must provide allocate(size_t, size_t) and deallocate(void*).
template <std::size_t Size, typename Allocator = StdAllocator>
class ListSlab {
  // Minimal node type for the intrusive free list. Any user type T with
  // slist_member_hook<> hook at offset 0 is layout-compatible, so we
  // reinterpret_cast between Node* and T* in Allocate/Deallocate.
  struct Node {
    boost::intrusive::slist_member_hook<> hook;
  };

  static_assert(Size >= sizeof(Node),
                "Size must be at least sizeof(slist_member_hook) to hold the free-list hook");

 public:
  explicit ListSlab(std::size_t capacity);
  ~ListSlab();

  // Non-copyable, non-movable.
  ListSlab(const ListSlab&) = delete;
  ListSlab& operator=(const ListSlab&) = delete;
  ListSlab(ListSlab&&) = delete;
  ListSlab& operator=(ListSlab&&) = delete;

  // Allocate one entry from the free list, returned as T*.
  // Returns nullptr if full.
  // Compile-time checks: sizeof(T) == Size, T has slist_member_hook<> hook
  // at offset 0.
  template <typename T>
  T* Allocate();

  // Return an entry to the free list.
  // Compile-time checks: sizeof(T) == Size, T has slist_member_hook<> hook
  // at offset 0.
  template <typename T>
  void Deallocate(T* entry);

  std::size_t free_count() const { return free_count_; }
  std::size_t used_count() const { return capacity_ - free_count_; }
  std::size_t capacity() const { return capacity_; }
  const uint8_t* slab_base() const { return slab_; }

 private:
  using FreeList = boost::intrusive::slist<
      Node,
      boost::intrusive::member_hook<
          Node, boost::intrusive::slist_member_hook<>, &Node::hook>,
      boost::intrusive::cache_last<false>>;

  uint8_t* slab_;        // contiguous byte array: capacity * Size bytes
  std::size_t capacity_;
  std::size_t free_count_;
  FreeList free_list_;
  Allocator allocator_;

  // Get the Node at slot index i.
  Node* NodeAt(std::size_t i) {
    return reinterpret_cast<Node*>(slab_ + i * Size);
  }
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

template <std::size_t Size, typename Allocator>
ListSlab<Size, Allocator>::ListSlab(std::size_t capacity)
    : slab_(nullptr), capacity_(capacity), free_count_(capacity) {
  void* raw = allocator_.allocate(capacity * Size, Size);
  slab_ = static_cast<uint8_t*>(raw);
  for (std::size_t i = 0; i < capacity; ++i) {
    auto* node = new (NodeAt(i)) Node{};
    free_list_.push_front(*node);
  }
}

template <std::size_t Size, typename Allocator>
ListSlab<Size, Allocator>::~ListSlab() {
  free_list_.clear();
  for (std::size_t i = 0; i < capacity_; ++i) {
    NodeAt(i)->~Node();
  }
  allocator_.deallocate(slab_);
}

template <std::size_t Size, typename Allocator>
template <typename T>
T* ListSlab<Size, Allocator>::Allocate() {
  static_assert(sizeof(T) == Size,
                "sizeof(T) must equal the slab's Size parameter");
  static_assert(
      std::is_same_v<decltype(T::hook),
                     boost::intrusive::slist_member_hook<>>,
      "T must have a public slist_member_hook<> member named 'hook'");
  static_assert(offsetof(T, hook) == 0,
                "T::hook must be at offset 0 (first member)");

  if (free_list_.empty()) return nullptr;
  Node& node = free_list_.front();
  free_list_.pop_front();
  --free_count_;
  return reinterpret_cast<T*>(&node);
}

template <std::size_t Size, typename Allocator>
template <typename T>
void ListSlab<Size, Allocator>::Deallocate(T* entry) {
  static_assert(sizeof(T) == Size,
                "sizeof(T) must equal the slab's Size parameter");
  static_assert(
      std::is_same_v<decltype(T::hook),
                     boost::intrusive::slist_member_hook<>>,
      "T must have a public slist_member_hook<> member named 'hook'");
  static_assert(offsetof(T, hook) == 0,
                "T::hook must be at offset 0 (first member)");

  auto* node = reinterpret_cast<Node*>(entry);
  free_list_.push_front(*node);
  ++free_count_;
}

}  // namespace rxtx

#endif  // RXTX_LIST_SLAB_H_
