// rxtx/allocator.h
#ifndef RXTX_ALLOCATOR_H_
#define RXTX_ALLOCATOR_H_

#include <cstddef>
#include <new>

#include <rte_malloc.h>

#include "rxtx/packet.h"  // kCacheLineSize

namespace rxtx {

// Standard heap allocator for unit tests.
struct StdAllocator {
  void* allocate(std::size_t bytes, std::size_t alignment) {
    return ::operator new(bytes, std::align_val_t{alignment});
  }
  void deallocate(void* ptr) {
    ::operator delete(ptr, std::align_val_t{kCacheLineSize});
  }
};

// DPDK huge-page allocator for production.
struct DpdkAllocator {
  void* allocate(std::size_t bytes, std::size_t alignment) {
    return rte_malloc("lookup_slab", bytes, alignment);
  }
  void deallocate(void* ptr) {
    rte_free(ptr);
  }
};

}  // namespace rxtx

#endif  // RXTX_ALLOCATOR_H_
