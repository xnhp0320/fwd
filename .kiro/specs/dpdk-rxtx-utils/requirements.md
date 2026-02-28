# Requirements Document

## Introduction

This document specifies requirements for DPDK RX/TX utility classes that provide a zero-copy, cache-friendly abstraction over DPDK mbufs. The feature introduces a `Packet` class constructed via placement new directly from mbufs received by `rte_eth_rx_burst`, and a compile-time-sized `Batch` class that wraps the burst array with efficient iteration and filtering operations. These utilities will be consumed by the PMD thread packet processing loop.

## Glossary

- **Packet**: A C++ class representing a single DPDK mbuf, constructed via placement new over the mbuf memory region
- **Batch**: A compile-time-sized container of `rte_mbuf*` pointers received from a single `rte_eth_rx_burst` call, directly compatible with DPDK burst APIs. Parameterized by `BatchSize` and an optional `SafeMode` boolean (default false)
- **SafeMode**: A compile-time boolean template parameter on the Batch class. When false (default), Append performs no bounds checking for maximum performance. When true, Append checks capacity and returns a boolean status
- **Mbuf**: A DPDK `rte_mbuf` structure (2 cache lines / 128 bytes) that describes a network packet buffer
- **Headroom**: The reserved space between the end of the mbuf structure and the start of packet payload data, controlled by `RTE_PKTMBUF_HEADROOM` (typically 128 bytes)
- **Metadata_Region**: A cache-line-aligned memory area placed in the mbuf headroom, immediately after the `rte_mbuf` structure, reserved for future per-packet application metadata
- **Batch_Size**: A compile-time constant (template parameter) defining the maximum number of packets in a Batch (e.g. 16)
- **Filter_Function**: A callable that takes a Packet reference and returns a boolean indicating whether the packet should be retained in the batch (rejected packets are not freed, only excluded from the compacted result)
- **Transform_Function**: A callable that takes a Packet reference and performs an operation on the packet

## Requirements

### Requirement 1: Packet Class Memory Layout

**User Story:** As a DPDK application developer, I want a Packet class that maps directly onto the mbuf memory layout using placement new, so that I can access packet data without additional memory allocations or pointer indirections.

#### Acceptance Criteria

1. THE Packet class SHALL be constructible via placement new at the address of an `rte_mbuf` pointer received from `rte_eth_rx_burst`
2. THE Packet class SHALL provide access to the underlying `rte_mbuf` structure without additional heap allocations
3. THE Packet class SHALL have a memory layout where the `rte_mbuf` occupies the first 2 cache lines (128 bytes)
4. THE Packet class SHALL reserve a Metadata_Region in the mbuf headroom immediately after the `rte_mbuf` structure, aligned to a cache line boundary
5. THE Packet class SHALL define the Metadata_Region size as zero bytes in the initial implementation, with the layout supporting future non-zero metadata
6. THE Packet class SHALL provide a static assertion verifying that the Metadata_Region fits within `RTE_PKTMBUF_HEADROOM`
7. THE Packet class SHALL NOT be copyable or default-constructible, as Packet instances are only valid when constructed over a live mbuf
8. THE Packet class SHALL provide a static `from` method (e.g. `Packet::from(rte_mbuf*)`) that performs placement new on the given mbuf pointer and returns a reference to the resulting Packet

### Requirement 2: Packet Class Data Access

**User Story:** As a DPDK application developer, I want convenient accessors on the Packet class for common mbuf fields, so that I can read packet properties without directly manipulating the raw mbuf.

#### Acceptance Criteria

1. THE Packet class SHALL provide a method to retrieve a pointer to the packet payload data
2. THE Packet class SHALL provide a method to retrieve the packet data length
3. THE Packet class SHALL provide a method to retrieve a pointer to the underlying `rte_mbuf`
4. THE Packet class SHALL provide a method to free the underlying mbuf back to its mempool

### Requirement 3: Batch Class Structure

**User Story:** As a DPDK application developer, I want a Batch class templated on a compile-time batch size that wraps the raw pointer array and count returned by `rte_eth_rx_burst`, so that I can manage burst results efficiently, with an optional SafeMode for development and testing.

#### Acceptance Criteria

1. THE Batch class SHALL be a class template parameterized by Batch_Size as a compile-time constant and an optional `bool SafeMode` parameter defaulting to false
2. THE Batch class SHALL contain a fixed-size array of `rte_mbuf*` pointers (not Packet pointers) with capacity equal to Batch_Size
3. THE Batch class SHALL store the actual count of received packets as returned by `rte_eth_rx_burst`
4. THE Batch class SHALL provide access to the raw `rte_mbuf**` pointer and a pointer to the count, suitable for passing directly to `rte_eth_rx_burst` and `rte_eth_tx_burst` without any conversion
5. THE Batch class SHALL use raw `rte_mbuf*` pointers for storage, without smart pointer wrappers or Packet pointer indirection
6. THE Batch class SHALL NOT be copyable, to prevent accidental double-free of mbufs
7. WHEN SafeMode is false (default), THE Batch class SHALL generate zero overhead for Append operations — no bounds checks, no branching, no return value
8. WHEN SafeMode is true, THE Batch class SHALL generate bounds-checked Append operations that return a boolean status

### Requirement 4: Batch ForEach Iteration

**User Story:** As a DPDK application developer, I want to iterate over all packets in a batch and apply a function to each one, so that I can perform per-packet processing efficiently.

#### Acceptance Criteria

1. THE Batch class SHALL provide a `ForEach` method that accepts a Transform_Function template parameter
2. WHEN `ForEach` is invoked, THE Batch class SHALL internally convert each `rte_mbuf*` to a Packet reference using `Packet::from` and call the Transform_Function with that reference, in order from index 0 to count-1
3. WHEN the batch count is zero, THE `ForEach` method SHALL return without invoking the Transform_Function

### Requirement 5: Batch Filter Iteration

**User Story:** As a DPDK application developer, I want to iterate over all packets in a batch and retain only those that pass a filter predicate, so that I can efficiently separate wanted from unwanted packets without freeing the rejected ones.

#### Acceptance Criteria

1. THE Batch class SHALL provide a `Filter` method that accepts a Filter_Function template parameter
2. WHEN `Filter` is invoked, THE Batch class SHALL internally convert each `rte_mbuf*` to a Packet reference using `Packet::from` and call the Filter_Function with that reference
3. WHEN the Filter_Function returns true for a Packet, THE Batch class SHALL retain that Packet in the batch
4. WHEN the Filter_Function returns false for a Packet, THE Batch class SHALL exclude that Packet from the compacted result without freeing the corresponding mbuf
5. AFTER `Filter` completes, THE Batch class SHALL update the packet count to reflect only retained packets
6. AFTER `Filter` completes, THE retained packets SHALL occupy contiguous positions starting from index 0 in the batch array
7. WHEN the batch count is zero, THE `Filter` method SHALL return without invoking the Filter_Function

### Requirement 6: Batch Lifecycle Management

**User Story:** As a DPDK application developer, I want the Batch class to properly manage mbuf lifetimes, so that mbufs are not leaked when a batch goes out of scope.

#### Acceptance Criteria

1. WHEN a Batch is destroyed, THE Batch class SHALL free all mbufs that remain in the batch
2. WHEN a Batch is destroyed with a count of zero, THE Batch class SHALL perform no mbuf free operations
3. THE Batch class SHALL provide a method to release ownership of all mbufs without freeing them, resetting the count to zero

### Requirement 7: Batch Append Operations

**User Story:** As a DPDK application developer, I want to append individual packets or raw mbufs to an existing batch, so that I can build up batches incrementally (e.g., collecting rejected packets from a filter into a separate batch), with the option to trade safety for maximum performance.

#### Acceptance Criteria

1. THE Batch class SHALL provide an `Append` method that accepts a `Packet&` and appends the Packet's underlying mbuf pointer to the batch
2. THE Batch class SHALL provide an `Append` method that accepts an `rte_mbuf*` and appends the raw mbuf pointer to the batch
3. WHEN `Append` is called, THE Batch class SHALL store the mbuf pointer at index `Count()` and increment the count by one
4. WHEN SafeMode is false (default), THE `Append` method SHALL return void and perform no bounds check — it stores the pointer and increments the count unconditionally for maximum performance
5. WHEN SafeMode is true, THE `Append` method SHALL check whether `Count()` equals `Capacity()` before appending; if the batch is full, it SHALL not append the mbuf and SHALL return false; if the batch is not full, it SHALL append and return true
6. THE Batch class SHALL use `if constexpr` to select between safe and unsafe Append behavior at compile time, ensuring zero overhead in the default (unsafe) mode

