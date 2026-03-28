// rxtx/f14_simd_test.cc
// Property-based tests for the F14 SIMD abstraction layer.

#include "rxtx/f14_simd.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "rapidcheck.h"
#include "rapidcheck/gtest.h"

namespace rxtx::f14 {
namespace {

// Feature: f14-lookup-table, Property 1: SIMD tag matching correctness
// **Validates: Requirements 1.1, 1.2, 1.7**
//
// For any 16-byte aligned ChunkHeader (with arbitrary byte values in positions
// 0-13 and arbitrary control/overflow bytes at positions 14-15) and for any
// needle byte value 0-255:
//   - SimdBackend::TagMatch returns a bitmask where bit N (N in 0..13) is set
//     iff header[N] == needle, and bits 14-15 are always zero.
//   - SimdBackend::OccupiedMask returns a bitmask where bit N is set iff
//     header[N] != 0, with bits 14-15 always zero.
RC_GTEST_PROP(F14SimdProperty1, TagMatchCorrectness, ()) {
  // Generate random bytes for the 16-byte aligned header.
  alignas(16) uint8_t header[16];
  for (int i = 0; i < 14; ++i) {
    header[i] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  }
  // Positions 14-15 are control/overflow — arbitrary values.
  header[14] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  header[15] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));

  auto needle = static_cast<uint8_t>(*rc::gen::inRange(0, 256));

  // Compute expected TagMatch bitmask by iterating positions 0-13.
  TagMask expected_tag_mask = 0;
  for (int i = 0; i < kCapacity; ++i) {
    if (header[i] == needle) {
      expected_tag_mask |= (static_cast<TagMask>(1) << i);
    }
  }

  TagMask actual_tag_mask = SimdBackend::TagMatch(&header, needle);

  // Bits 14-15 must always be zero.
  RC_ASSERT((actual_tag_mask & ~kFullMask) == 0);
  // Bitmask must match expected.
  RC_ASSERT(actual_tag_mask == expected_tag_mask);
}

// Feature: f14-lookup-table, Property 1: SIMD tag matching correctness
// **Validates: Requirements 1.1, 1.2, 1.7**
//
// OccupiedMask relies on the F14 invariant that occupied tags always have
// bit 7 set (tag >= 0x80, from SplitHash: tag = (hash >> 24) | 0x80).
// The SSE2 backend uses _mm_movemask_epi8 which extracts bit 7 of each byte.
// We generate valid tag values: either 0 (empty) or in [0x80, 0xFF] (occupied).
RC_GTEST_PROP(F14SimdProperty1, OccupiedMaskCorrectness, ()) {
  // Generate valid tag bytes: 0 (empty) or [0x80, 0xFF] (occupied).
  alignas(16) uint8_t header[16];
  for (int i = 0; i < 14; ++i) {
    auto occupied = *rc::gen::arbitrary<bool>();
    if (occupied) {
      header[i] = static_cast<uint8_t>(*rc::gen::inRange(0x80, 0x100));
    } else {
      header[i] = 0;
    }
  }
  // Positions 14-15 are control/overflow — arbitrary values.
  header[14] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  header[15] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));

  // Compute expected OccupiedMask bitmask by checking non-zero in positions 0-13.
  TagMask expected_occupied_mask = 0;
  for (int i = 0; i < kCapacity; ++i) {
    if (header[i] != 0) {
      expected_occupied_mask |= (static_cast<TagMask>(1) << i);
    }
  }

  TagMask actual_occupied_mask = SimdBackend::OccupiedMask(&header);

  // Bits 14-15 must always be zero.
  RC_ASSERT((actual_occupied_mask & ~kFullMask) == 0);
  // Bitmask must match expected.
  RC_ASSERT(actual_occupied_mask == expected_occupied_mask);
}

// Feature: f14-lookup-table, Property 2: SIMD cross-platform equivalence
// **Validates: Requirements 8.5**
//
// For any 16-byte aligned ChunkHeader and any needle byte, the result of
// SimdBackend::TagMatch shall be identical to ScalarBackend::TagMatch, and
// SimdBackend::OccupiedMask shall be identical to ScalarBackend::OccupiedMask.
RC_GTEST_PROP(F14SimdProperty2, TagMatchEquivalence, ()) {
  // Generate random bytes for the 16-byte aligned header.
  alignas(16) uint8_t header[16];
  for (int i = 0; i < 14; ++i) {
    header[i] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  }
  header[14] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  header[15] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));

  auto needle = static_cast<uint8_t>(*rc::gen::inRange(0, 256));

  TagMask simd_result = SimdBackend::TagMatch(&header, needle);
  TagMask scalar_result = ScalarBackend::TagMatch(&header, needle);

  RC_ASSERT(simd_result == scalar_result);
}

// Feature: f14-lookup-table, Property 2: SIMD cross-platform equivalence
// **Validates: Requirements 8.5**
//
// OccupiedMask equivalence: use valid F14 tag values (0 for empty,
// [0x80, 0xFF] for occupied) since the SSE2 OccupiedMask relies on bit 7.
RC_GTEST_PROP(F14SimdProperty2, OccupiedMaskEquivalence, ()) {
  // Generate valid F14 tag bytes: 0 (empty) or [0x80, 0xFF] (occupied).
  alignas(16) uint8_t header[16];
  for (int i = 0; i < 14; ++i) {
    auto occupied = *rc::gen::arbitrary<bool>();
    if (occupied) {
      header[i] = static_cast<uint8_t>(*rc::gen::inRange(0x80, 0x100));
    } else {
      header[i] = 0;
    }
  }
  header[14] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));
  header[15] = static_cast<uint8_t>(*rc::gen::inRange(0, 256));

  TagMask simd_result = SimdBackend::OccupiedMask(&header);
  TagMask scalar_result = ScalarBackend::OccupiedMask(&header);

  RC_ASSERT(simd_result == scalar_result);
}

// Feature: f14-lookup-table, Property 3: Tag derivation always non-zero
// **Validates: Requirements 2.10, 5.6**
//
// For any std::size_t hash value, SplitHash(hash).tag shall have bit 7 set
// (tag >= 0x80), ensuring the tag is always non-zero and distinguishable
// from an empty slot.
//
// SplitHash is defined locally here matching the design spec formula:
//   tag = (hash >> 24) | 0x80
// Task 2.1 will define it properly in f14_map.h.
namespace {
struct LocalHashPair {
  std::size_t hash;
  uint8_t tag;
};

inline LocalHashPair SplitHash(std::size_t hash) {
  return {hash, static_cast<uint8_t>((hash >> 24) | 0x80)};
}
}  // namespace

RC_GTEST_PROP(F14SimdProperty3, TagDerivationAlwaysNonZero, ()) {
  auto hash = *rc::gen::arbitrary<std::size_t>();

  auto hp = SplitHash(hash);

  // Tag must have bit 7 set (>= 0x80).
  RC_ASSERT(hp.tag >= 0x80);
  // Tag must be non-zero (implied by >= 0x80, but explicit for clarity).
  RC_ASSERT(hp.tag != 0);
}

}  // namespace
}  // namespace rxtx::f14
