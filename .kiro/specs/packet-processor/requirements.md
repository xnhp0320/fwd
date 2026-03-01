# Requirements Document

## Introduction

This feature introduces a packet processor abstraction for the DPDK-based application. Each PMD thread owns a packet processor instance that defines how packets are received, processed, and transmitted. The design prioritizes data-plane performance by avoiding virtual function dispatch overhead, using compile-time polymorphism (CRTP) or template-based dispatch instead. A factory/registry mechanism allows the PMD config file to specify the processor class by name, enabling config-driven selection of forwarding strategies. The first concrete implementation is a simple forwarding processor that drains all RX queues into a single TX queue.

## Glossary

- **Packet_Processor**: The base class template or interface that defines the contract for packet processing within a PMD thread. It provides a queue-check function and a process function.
- **Simple_Forwarding_Processor**: A concrete packet processor that receives packets from all assigned RX queues and forwards them to a single TX queue.
- **PMD_Thread**: A poll-mode driver thread running on a dedicated lcore, responsible for packet I/O. Each PMD thread owns one Packet_Processor instance.
- **Queue_Assignment**: A (port_id, queue_id) pair representing an RX or TX queue assigned to a PMD thread.
- **Processor_Registry**: A name-to-factory mapping that allows instantiation of Packet_Processor implementations by string name from configuration.
- **Check_Function**: A function that validates whether the current set of RX and TX queue assignments satisfies the requirements of a specific Packet_Processor implementation.
- **Process_Function**: A function that performs one iteration of the packet processing loop: receive from RX queues, process, and transmit to TX queues.
- **CRTP**: Curiously Recurring Template Pattern, a C++ technique for compile-time polymorphism that avoids virtual function overhead.
- **Batch**: A fixed-capacity array of rte_mbuf pointers used for burst RX/TX operations (defined in `rxtx/batch.h`).

## Requirements

### Requirement 1: Packet Processor Interface

**User Story:** As a developer, I want a packet processor abstraction with a well-defined interface, so that I can implement different forwarding strategies without modifying the PMD thread loop.

#### Acceptance Criteria

1. THE Packet_Processor SHALL expose a Check_Function that accepts the PMD thread's RX and TX Queue_Assignment vectors and returns a status indicating whether the assignments satisfy the processor's requirements.
2. THE Packet_Processor SHALL expose a Process_Function that performs one iteration of packet receive, process, and transmit using the assigned queues.
3. THE Packet_Processor SHALL be instantiated once per PMD_Thread and owned by that PMD_Thread for the thread's lifetime.
4. THE Packet_Processor SHALL accept the PmdThreadConfig at construction time to access its queue assignments.

### Requirement 2: Performance-Oriented Dispatch

**User Story:** As a developer, I want the packet processor dispatch to avoid virtual function call overhead on the data-plane hot path, so that packet processing throughput is maximized.

#### Acceptance Criteria

1. THE Packet_Processor SHALL use a compile-time polymorphism mechanism (such as CRTP or template-based dispatch) for the Process_Function to eliminate virtual function call overhead on the hot path.
2. THE Packet_Processor SHALL allow the Check_Function to use runtime dispatch (virtual or type-erased) since it is called only at initialization time, not on the hot path.
3. THE Packet_Processor SHALL ensure that the Process_Function is inlineable by the compiler when the concrete type is known at the call site.

### Requirement 3: Processor Registry and Config-Driven Selection

**User Story:** As an operator, I want to specify the packet processor class by name in the PMD config file, so that I can change forwarding behavior without recompiling.

#### Acceptance Criteria

1. THE Processor_Registry SHALL maintain a mapping from string names to factory functions that create Packet_Processor instances.
2. WHEN a PMD_Thread configuration includes a processor name field, THE Processor_Registry SHALL look up the name and return a factory function for the corresponding Packet_Processor implementation.
3. IF the PMD_Thread configuration specifies a processor name that is not registered, THEN THE Processor_Registry SHALL return an error status with a descriptive message listing the unknown name.
4. THE Processor_Registry SHALL support registration of new Packet_Processor implementations without modifying existing registry code (open-closed principle).
5. WHEN no processor name is specified in the PMD_Thread configuration, THE Processor_Registry SHALL use a default processor name.

### Requirement 4: Config Schema Extension for Processor Name

**User Story:** As an operator, I want to specify the processor name in the PMD thread JSON configuration, so that each PMD thread can use a different forwarding strategy.

#### Acceptance Criteria

1. THE PmdThreadConfig SHALL include an optional string field for the processor name.
2. WHEN the JSON configuration for a PMD thread contains a "processor" field, THE Config_Parser SHALL parse the field value as the processor name string and store it in PmdThreadConfig.
3. WHEN the JSON configuration for a PMD thread omits the "processor" field, THE Config_Parser SHALL leave the processor name field empty (indicating the default processor should be used).

### Requirement 5: Queue Assignment Validation at Startup

**User Story:** As an operator, I want the system to validate queue assignments against the processor's requirements at startup, so that misconfiguration is caught early before the data-plane loop begins.

#### Acceptance Criteria

1. WHEN a PMD_Thread is initialized with a Packet_Processor, THE PMD_Thread SHALL call the Check_Function with its RX and TX Queue_Assignment vectors before entering the processing loop.
2. IF the Check_Function returns a failure status, THEN THE PMD_Thread SHALL log the error and abort the thread launch, returning the error to the PMDThreadManager.
3. THE Check_Function SHALL return a descriptive error message indicating which queue requirement is not met (e.g., "SimpleForwardingProcessor requires exactly 1 TX queue, but 3 were assigned").

### Requirement 6: Simple Forwarding Processor

**User Story:** As a developer, I want a simple forwarding processor that reads packets from all RX queues and sends them to a single TX queue, so that I have a basic working processor for testing and simple use cases.

#### Acceptance Criteria

1. THE Simple_Forwarding_Processor SHALL register itself in the Processor_Registry under the name "simple_forwarding".
2. WHEN the Check_Function is called, THE Simple_Forwarding_Processor SHALL verify that exactly 1 TX Queue_Assignment is present and return a failure status with a descriptive message if the count is not 1.
3. WHEN the Check_Function is called, THE Simple_Forwarding_Processor SHALL accept any number of RX Queue_Assignments (including zero).
4. WHEN the Process_Function is called, THE Simple_Forwarding_Processor SHALL receive a Batch of packets from each assigned RX queue using rte_eth_rx_burst.
5. WHEN the Process_Function is called, THE Simple_Forwarding_Processor SHALL transmit all received packets to the single assigned TX queue using rte_eth_tx_burst.
6. IF rte_eth_tx_burst transmits fewer packets than provided, THEN THE Simple_Forwarding_Processor SHALL free the untransmitted packets by calling rte_pktmbuf_free on each remaining mbuf.

### Requirement 7: PMD Thread Integration

**User Story:** As a developer, I want the PMD thread to delegate its packet processing loop to the Packet_Processor, so that the thread loop is generic and processor-agnostic.

#### Acceptance Criteria

1. THE PMD_Thread SHALL create a Packet_Processor instance using the Processor_Registry based on its PmdThreadConfig processor name field.
2. WHILE the PMD_Thread is in its main processing loop, THE PMD_Thread SHALL call the Packet_Processor Process_Function on each iteration.
3. THE PMD_Thread SHALL pass the stop flag check and the Process_Function call as the only operations in the hot loop (no additional branching or indirection beyond what the Packet_Processor requires).
