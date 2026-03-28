#ifndef RXTX_F14_SIMD_H_
#define RXTX_F14_SIMD_H_

// F14 SIMD abstraction layer: compile-time backend selection for
// tag matching and occupied-mask extraction on chunk headers.
//
// Backends:
//   Sse2Backend  — x86-64 SSE2 intrinsics
//   NeonBackend  — ARM aarch64 NEON intrinsics
//   ScalarBackend — portable byte-by-byte fallback
//
// SimdBackend is a compile-time alias to the best available backend.

#include <cstdint>
#include <cstring>

#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace rxtx::f14 {

// Bitmask type for tag match results (low 14 bits significant).
using TagMask = uint32_t;

// Low 14 bits set — one bit per item slot in a chunk.
inline constexpr TagMask kFullMask = 0x3FFF;  // (1 << 14) - 1

// Number of item slots per chunk.
inline constexpr int kCapacity = 14;

// Target load per chunk (capacity - 2).
inline constexpr int kDesiredCapacity = 12;

// ---------------------------------------------------------------------------
// ScalarBackend — portable fallback, byte-by-byte loop over kCapacity bytes.
// ---------------------------------------------------------------------------
struct ScalarBackend {
  /// Compare all 14 tag bytes against needle; return bitmask of matches.
  static TagMask TagMatch(const void* header, uint8_t needle) {
    const auto* tags = reinterpret_cast<const uint8_t*>(header);
    TagMask mask = 0;
    for (int i = 0; i < kCapacity; ++i) {
      if (tags[i] == needle) {
        mask |= (static_cast<TagMask>(1) << i);
      }
    }
    return mask;
  }

  /// Return bitmask of slots with non-zero tags (occupied).
  static TagMask OccupiedMask(const void* header) {
    const auto* tags = reinterpret_cast<const uint8_t*>(header);
    TagMask mask = 0;
    for (int i = 0; i < kCapacity; ++i) {
      if (tags[i] != 0) {
        mask |= (static_cast<TagMask>(1) << i);
      }
    }
    return mask;
  }
};

// ---------------------------------------------------------------------------
// Sse2Backend — x86-64 SSE2 intrinsics for parallel 16-byte comparison.
// ---------------------------------------------------------------------------
#if defined(__SSE2__)
struct Sse2Backend {
  /// Compare all 14 tag bytes against needle using SSE2.
  /// _mm_movemask_epi8 extracts the high bit of each byte into a 16-bit mask;
  /// we mask to kFullMask to keep only the 14 tag positions.
  static TagMask TagMatch(const void* header, uint8_t needle) {
    __m128i tags = _mm_load_si128(reinterpret_cast<const __m128i*>(header));
    __m128i needle_v = _mm_set1_epi8(static_cast<char>(needle));
    __m128i eq = _mm_cmpeq_epi8(tags, needle_v);
    return static_cast<TagMask>(_mm_movemask_epi8(eq)) & kFullMask;
  }

  /// Return bitmask of occupied slots.  Tags have bit 7 set (tag >= 0x80)
  /// when occupied, so _mm_movemask_epi8 directly extracts occupancy.
  static TagMask OccupiedMask(const void* header) {
    __m128i tags = _mm_load_si128(reinterpret_cast<const __m128i*>(header));
    return static_cast<TagMask>(_mm_movemask_epi8(tags)) & kFullMask;
  }
};
#endif  // __SSE2__

// ---------------------------------------------------------------------------
// NeonBackend — ARM aarch64 NEON intrinsics for parallel 16-byte comparison.
// Uses vshrn_n_u16 narrowing shift to extract a per-byte bitmask.
// ---------------------------------------------------------------------------
#if defined(__ARM_NEON)
struct NeonBackend {
  /// Compare all 14 tag bytes against needle using NEON.
  /// vceqq_u8 produces 0xFF per matching byte, 0x00 otherwise.
  /// We extract one bit per byte via vshrn_n_u16 narrowing shift,
  /// then reconstruct a scalar bitmask.
  static TagMask TagMatch(const void* header, uint8_t needle) {
    uint8x16_t tags = vld1q_u8(reinterpret_cast<const uint8_t*>(header));
    uint8x16_t needle_v = vdupq_n_u8(needle);
    uint8x16_t eq = vceqq_u8(tags, needle_v);
    return NeonMaskExtract(eq) & kFullMask;
  }

  /// Return bitmask of occupied slots.  Occupied tags have bit 7 set
  /// (tag >= 0x80), so we test the high bit of each byte.
  static TagMask OccupiedMask(const void* header) {
    uint8x16_t tags = vld1q_u8(reinterpret_cast<const uint8_t*>(header));
    // Shift each byte right by 7 to isolate the high bit, then
    // use the same narrowing extraction to get a scalar bitmask.
    // For occupied: tag != 0 means bit 7 is set (tags are always >= 0x80).
    // We reinterpret as signed and compare > 0 won't work since 0x80+ is
    // negative in signed. Instead, directly extract bit 7 via the mask.
    uint8x16_t high_bits = vshrq_n_u8(tags, 7);
    // high_bits[i] is 1 if tag[i] had bit 7 set, 0 otherwise.
    // Multiply by 0xFF to get the same 0xFF/0x00 pattern as vceqq.
    uint8x16_t mask_vec = vmulq_u8(high_bits, vdupq_n_u8(0xFF));
    return NeonMaskExtract(mask_vec) & kFullMask;
  }

 private:
  /// Extract a scalar bitmask from a NEON 0xFF/0x00 comparison result.
  /// Uses vshrn_n_u16 to narrow 16-bit pairs, then reconstructs bits.
  static TagMask NeonMaskExtract(uint8x16_t cmp_result) {
    // Narrow: take each pair of bytes as a uint16, shift right by 4,
    // and store the low 8 bits.  This packs 16 bytes into 8 bytes.
    uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(cmp_result), 4);
    uint64_t bits = vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);

    // Each original byte that was 0xFF becomes a nibble pattern in the
    // narrowed result.  We extract one bit per original byte:
    //   - Even-indexed bytes (0,2,4,...) contribute to the low nibble → bit 3
    //   - Odd-indexed bytes (1,3,5,...) contribute to the high nibble → bit 7
    TagMask mask = 0;
    for (int i = 0; i < 8; ++i) {
      uint8_t byte = static_cast<uint8_t>(bits >> (i * 8));
      if (byte & 0x08) mask |= (static_cast<TagMask>(1) << (2 * i));
      if (byte & 0x80) mask |= (static_cast<TagMask>(1) << (2 * i + 1));
    }
    return mask;
  }
};
#endif  // __ARM_NEON

// ---------------------------------------------------------------------------
// Compile-time backend selection.
// ---------------------------------------------------------------------------
#if defined(__SSE2__)
using SimdBackend = Sse2Backend;
#elif defined(__ARM_NEON)
using SimdBackend = NeonBackend;
#else
using SimdBackend = ScalarBackend;
#endif

}  // namespace rxtx::f14

#endif  // RXTX_F14_SIMD_H_
