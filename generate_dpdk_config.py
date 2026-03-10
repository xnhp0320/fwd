#!/usr/bin/env python3
"""CLI tool to generate DPDK configuration files.

This script provides a command-line interface to generate dpdk.json
configuration files using the TestConfigGenerator from the test fixtures.

Example usage:
    ./generate_dpdk_config.py --ports 2 --threads 2 --queues 2 -o dpdk.json
    ./generate_dpdk_config.py --ports 1 --threads 1 --queues 1 --hugepages
"""

import argparse
import sys
from pathlib import Path

# Add tests/fixtures to Python path to import TestConfigGenerator
sys.path.insert(0, str(Path(__file__).parent / "tests" / "fixtures"))

try:
    from config_generator import TestConfigGenerator
except ImportError as e:
    print(f"Error: Could not import TestConfigGenerator from tests/fixtures/config_generator.py", file=sys.stderr)
    print(f"Details: {e}", file=sys.stderr)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Generate DPDK configuration file with net_tap virtual PMD",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate default config (2 ports, 2 threads, 2 queues)
  %(prog)s

  # Single port, single thread configuration
  %(prog)s --ports 1 --threads 1 --queues 1

  # Custom output path with hugepages enabled
  %(prog)s --ports 2 --threads 4 --queues 4 --hugepages -o custom.json

  # Different RX and TX queue counts
  %(prog)s --ports 2 --threads 4 --rx-queues 8 --tx-queues 4
        """
    )
    
    parser.add_argument(
        "--ports",
        type=int,
        default=2,
        help="Number of virtual ports (default: 2, min: 1)"
    )
    
    parser.add_argument(
        "--threads",
        type=int,
        default=2,
        help="Number of PMD worker threads (default: 2, min: 1)"
    )
    
    parser.add_argument(
        "--queues",
        type=int,
        default=2,
        help="Number of queues per port for both RX and TX (default: 2, min: 1)"
    )
    
    parser.add_argument(
        "--rx-queues",
        type=int,
        help="Number of RX queues per port (overrides --queues for RX, min: 1)"
    )
    
    parser.add_argument(
        "--tx-queues",
        type=int,
        help="Number of TX queues per port (overrides --queues for TX, min: 1)"
    )
    
    parser.add_argument(
        "--hugepages",
        action="store_true",
        help="Use hugepages (default: disabled, uses --no-huge)"
    )
    
    parser.add_argument(
        "--processor",
        type=str,
        help="Processor name (e.g., five_tuple_forwarding)"
    )
    
    parser.add_argument(
        "--processor-param",
        action="append",
        metavar="KEY=VALUE",
        help="Processor parameter as KEY=VALUE (repeatable)"
    )
    
    parser.add_argument(
        "--session-capacity",
        type=int,
        default=0,
        help="Session table capacity (0 = disabled)"
    )
    
    parser.add_argument(
        "-o", "--output",
        default="dpdk.json",
        help="Output file path (default: dpdk.json)"
    )
    
    args = parser.parse_args()
    
    # Validate minimum values
    if args.ports < 1:
        parser.error("--ports must be >= 1")
    if args.threads < 1:
        parser.error("--threads must be >= 1")
    if args.queues < 1:
        parser.error("--queues must be >= 1")
    if args.rx_queues is not None and args.rx_queues < 1:
        parser.error("--rx-queues must be >= 1")
    if args.tx_queues is not None and args.tx_queues < 1:
        parser.error("--tx-queues must be >= 1")

    # Parse processor parameters into a dict
    processor_params = None
    if args.processor_param:
        processor_params = {}
        for param in args.processor_param:
            if '=' not in param:
                print(f"Error: Invalid processor parameter format: {param} (expected KEY=VALUE)", file=sys.stderr)
                sys.exit(1)
            key, value = param.split('=', 1)
            processor_params[key] = value

    try:
        # Generate configuration
        config = TestConfigGenerator.generate_config(
            num_ports=args.ports,
            num_threads=args.threads,
            num_queues=args.queues,
            num_rx_queues=args.rx_queues,
            num_tx_queues=args.tx_queues,
            use_hugepages=args.hugepages,
            processor_name=args.processor,
            processor_params=processor_params,
            session_capacity=args.session_capacity
        )
        
        # Write to file
        TestConfigGenerator.write_config(config, args.output)
        
        # Print success message with configuration summary
        print(f"✓ Generated DPDK configuration: {args.output}")
        print(f"  Ports: {args.ports}")
        print(f"  Threads: {args.threads}")
        rx_q = args.rx_queues if args.rx_queues else args.queues
        tx_q = args.tx_queues if args.tx_queues else args.queues
        print(f"  RX Queues per port: {rx_q}")
        print(f"  TX Queues per port: {tx_q}")
        print(f"  Hugepages: {'enabled' if args.hugepages else 'disabled'}")
        if args.processor:
            print(f"  Processor: {args.processor}")
        if processor_params:
            print(f"  Processor params: {processor_params}")
        
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
