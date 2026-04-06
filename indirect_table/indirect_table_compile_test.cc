// Minimal compilation test for IndirectTable template instantiation.
#include "indirect_table/indirect_table.h"

struct TestKey {
  uint32_t id;
  friend bool operator==(const TestKey& a, const TestKey& b) {
    return a.id == b.id;
  }
};

struct TestKeyHash {
  std::size_t operator()(const TestKey& k) const { return std::hash<uint32_t>{}(k.id); }
};

struct TestValue {
  uint64_t data;
  friend bool operator==(const TestValue& a, const TestValue& b) {
    return a.data == b.data;
  }
};

struct TestValueHash {
  std::size_t operator()(const TestValue& v) const { return std::hash<uint64_t>{}(v.data); }
};

// Force template instantiation.
template class indirect_table::IndirectTable<
    TestKey, TestValue, TestKeyHash, std::equal_to<TestKey>,
    TestValueHash, std::equal_to<TestValue>>;

int main() { return 0; }
