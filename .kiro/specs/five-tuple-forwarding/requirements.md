# Requirements Document

## Introduction

This feature adds a FiveTupleForwarding packet processor that extends the existing processor framework with flow-table awareness. Unlike SimpleForwardingProcessor which blindly forwards packets, FiveTupleForwarding parses each packet's five-tuple (src_ip, dst_ip, src_port, dst_port, protocol) plus VNI, performs a lookup in a FastLookupTable, inserts new flows on miss, and forwards all packets to the first TX queue. The feature also introduces per-processor configuration parameters in the JSON config, a command-tag system for the control plane, and a CLI command to list commands by tag.

## Glossary

- **FiveTupleForwarding_Processor**: A packet processor that parses packet metadata, performs flow-table lookup/insert via FastLookupTable, and forwards packets to the first TX queue.
- **SimpleForwarding_Processor**: The existing packet processor that forwards packets without flow-table operations.
- **FastLookupTable**: A slab-backed hash-set data structure that stores LookupEntry records keyed by five-tuple + VNI.
- **PacketMetadata**: A 56-byte structure holding parsed five-tuple, VNI, VLAN, and flag fields extracted from a raw packet.
- **Processor_Registry**: The singleton that maps processor name strings to launcher/checker function pairs.
- **Config_Parser**: The JSON configuration parser that produces DpdkConfig structures.
- **Config_Validator**: The component that validates semantic correctness of parsed configuration.
- **Command_Handler**: The control-plane component that dispatches JSON commands to handler functions.
- **Control_Plane**: The main-lcore event loop that hosts the Unix socket server and Command_Handler.
- **CLI_Tool**: The fwdcli Go binary that communicates with the Control_Plane over a Unix domain socket.
- **Command_Tag**: A string attribute attached to each command indicating its category (e.g., "common", "five_tuple_forwarding").
- **Processor_Config**: A per-processor key-value parameter map parsed from the JSON config file alongside each pmd_thread entry.

## Requirements

### Requirement 1: FiveTupleForwarding Processor Registration

**User Story:** As a developer, I want a FiveTupleForwarding processor registered in the Processor_Registry, so that PMD threads can be configured to use it by name.

#### Acceptance Criteria

1. THE Processor_Registry SHALL contain an entry named "five_tuple_forwarding" after static initialization.
2. WHEN the Processor_Registry looks up "five_tuple_forwarding", THE Processor_Registry SHALL return a valid ProcessorEntry with non-null launcher and checker functions.

### Requirement 2: FiveTupleForwarding Packet Processing

**User Story:** As a developer, I want the FiveTupleForwarding processor to parse packets, perform flow-table operations, and forward packets, so that flow state is tracked per five-tuple.

#### Acceptance Criteria

1. WHEN a batch of packets is received from an RX queue, THE FiveTupleForwarding_Processor SHALL parse each packet into a PacketMetadata structure using PacketMetadata::Parse.
2. WHEN a PacketMetadata is successfully parsed, THE FiveTupleForwarding_Processor SHALL call FastLookupTable::Find with the parsed metadata.
3. WHEN FastLookupTable::Find returns nullptr for a parsed PacketMetadata, THE FiveTupleForwarding_Processor SHALL call FastLookupTable::Insert with the five-tuple fields from the metadata.
4. THE FiveTupleForwarding_Processor SHALL forward all received packets to the first TX queue (tx_queues[0]).
5. WHEN rte_eth_tx_burst transmits fewer packets than the batch count, THE FiveTupleForwarding_Processor SHALL free each untransmitted mbuf.
6. WHEN PacketMetadata::Parse returns a non-OK result, THE FiveTupleForwarding_Processor SHALL skip the flow-table lookup for that packet and still forward the packet to the first TX queue.

### Requirement 3: FiveTupleForwarding Check Validation

**User Story:** As a developer, I want the FiveTupleForwarding processor to validate its queue configuration at startup, so that misconfiguration is caught before entering the hot loop.

#### Acceptance Criteria

1. WHEN check_impl is called with an empty tx_queues vector, THE FiveTupleForwarding_Processor SHALL return an InvalidArgument error indicating that at least one TX queue is required.
2. WHEN check_impl is called with a non-empty tx_queues vector, THE FiveTupleForwarding_Processor SHALL return OK status.

### Requirement 4: Per-Processor Configuration Parameters

**User Story:** As a developer, I want to specify per-processor parameters in the JSON config file, so that processors like FiveTupleForwarding can receive custom settings such as table capacity.

#### Acceptance Criteria

1. WHEN a pmd_thread entry in the JSON config contains a "processor_params" object, THE Config_Parser SHALL parse the object into a key-value map stored in PmdThreadConfig.
2. WHEN a pmd_thread entry does not contain a "processor_params" field, THE Config_Parser SHALL set the processor parameters map to empty.
3. THE PmdThreadConfig structure SHALL include a field named processor_params of type map from string to string.

### Requirement 5: Processor Parameter Validation via Check Function

**User Story:** As a developer, I want each processor to validate its own parameters through a check function, so that unsupported parameters are rejected at startup.

#### Acceptance Criteria

1. THE Processor_Registry ProcessorEntry SHALL include a parameter-check function that accepts a processor parameter map and returns a status.
2. WHEN the FiveTupleForwarding_Processor parameter-check function receives a map containing a "capacity" key with a valid positive integer value, THE FiveTupleForwarding_Processor SHALL return OK status.
3. WHEN the FiveTupleForwarding_Processor parameter-check function receives a map containing an unrecognized key, THE FiveTupleForwarding_Processor SHALL return an InvalidArgument error listing the unrecognized key.
4. WHEN the SimpleForwarding_Processor parameter-check function receives a map containing a "capacity" key, THE SimpleForwarding_Processor SHALL return an InvalidArgument error indicating that "capacity" is not a supported parameter.
5. WHEN any processor parameter-check function receives an empty map, THE processor parameter-check function SHALL return OK status.
6. WHEN the Config_Validator validates a PmdThreadConfig, THE Config_Validator SHALL invoke the corresponding processor's parameter-check function with the parsed processor_params map.

### Requirement 6: FiveTupleForwarding Table Capacity Configuration

**User Story:** As a developer, I want to configure the FastLookupTable capacity for FiveTupleForwarding via the JSON config, so that table size can be tuned per deployment.

#### Acceptance Criteria

1. WHEN the FiveTupleForwarding_Processor is constructed with a PmdThreadConfig containing a "capacity" processor parameter, THE FiveTupleForwarding_Processor SHALL create a FastLookupTable with the specified capacity.
2. WHEN the FiveTupleForwarding_Processor is constructed with a PmdThreadConfig that does not contain a "capacity" processor parameter, THE FiveTupleForwarding_Processor SHALL create a FastLookupTable with a default capacity of 65536.
3. WHEN the "capacity" processor parameter contains a non-positive or non-integer value, THE FiveTupleForwarding_Processor parameter-check function SHALL return an InvalidArgument error.

### Requirement 7: Command Tag System

**User Story:** As a developer, I want each control-plane command to have a tag attribute, so that commands can be categorized and filtered by processor type.

#### Acceptance Criteria

1. THE Command_Handler SHALL associate a tag string with each registered command.
2. THE Command_Handler SHALL tag the existing commands (shutdown, status, get_threads, get_stats) with the tag "common".
3. THE Command_Handler SHALL provide a method to retrieve the list of all registered command names filtered by a given tag.
4. THE Command_Handler SHALL provide a method to retrieve the list of all registered command names with their associated tags.

### Requirement 8: FiveTupleForwarding Control-Plane Command

**User Story:** As a developer, I want a control-plane command to query the FiveTupleForwarding flow table, so that I can inspect flow state at runtime.

#### Acceptance Criteria

1. THE Command_Handler SHALL register a command named "get_flow_table" with the tag "five_tuple_forwarding".
2. WHEN the "get_flow_table" command is received and the active packet processor is FiveTupleForwarding_Processor, THE Command_Handler SHALL return a success response containing the current flow-table entry count.
3. WHEN the "get_flow_table" command is received and the active packet processor is not FiveTupleForwarding_Processor, THE Command_Handler SHALL return an error response with status "not_supported".

### Requirement 9: CLI Command to List Commands by Tag

**User Story:** As a developer, I want a CLI command to list supported control-plane commands filtered by tag, so that I can discover available commands for the active processor.

#### Acceptance Criteria

1. THE CLI_Tool SHALL provide a "commands" subcommand.
2. WHEN the "commands" subcommand is invoked without a tag filter, THE CLI_Tool SHALL send a "list_commands" request to the Control_Plane and display all commands with their tags.
3. WHEN the "commands" subcommand is invoked with a "--tag" flag, THE CLI_Tool SHALL send a "list_commands" request with the tag parameter and display only commands matching the specified tag.
4. THE CLI_Tool SHALL format the command list output as a table with columns for command name and tag.
5. WHEN the "--json" flag is set, THE CLI_Tool SHALL output the command list as raw JSON.

### Requirement 10: List Commands Control-Plane Command

**User Story:** As a developer, I want a control-plane command to list registered commands, so that the CLI tool can discover available commands.

#### Acceptance Criteria

1. THE Command_Handler SHALL register a command named "list_commands" with the tag "common".
2. WHEN the "list_commands" command is received without a "tag" parameter, THE Command_Handler SHALL return a success response containing all registered command names and their tags.
3. WHEN the "list_commands" command is received with a "tag" parameter, THE Command_Handler SHALL return a success response containing only the command names matching the specified tag.
