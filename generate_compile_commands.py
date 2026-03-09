#!/usr/bin/env python3
"""Generate compile_commands.json from Bazel CppCompile actions.

This script is Bazel 9 friendly and avoids dependencies on external extractor
rules that still rely on removed native Python rules.
"""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List


def build_action_expression(target_patterns: List[str]) -> str:
    if not target_patterns:
        base_expr = 'kind("cc_.* rule", //...)'
    elif len(target_patterns) == 1:
        base_expr = f'kind("cc_.* rule", {target_patterns[0]})'
    else:
        base_expr = f'kind("cc_.* rule", {" + ".join(target_patterns)})'
    return f'mnemonic("CppCompile", {base_expr})'


def extract_source_file(arguments: List[str]) -> str | None:
    for i, arg in enumerate(arguments[:-1]):
        if arg == "-c":
            return arguments[i + 1]
    return None


def resolve_workspace() -> Path:
    result = subprocess.run(
        ["bazel", "info", "workspace"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip()).resolve()


def resolve_execution_root() -> Path:
    result = subprocess.run(
        ["bazel", "info", "execution_root"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip()).resolve()


def run_aquery(action_expression: str, workspace_root: Path) -> dict:
    result = subprocess.run(
        ["bazel", "aquery", "--output=jsonproto", action_expression],
        cwd=workspace_root,
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def collect_entries(
    aquery_data: dict, workspace_root: Path, execution_root: Path
) -> list[dict]:
    entries_by_file: dict[str, dict] = {}

    for action in aquery_data.get("actions", []):
        arguments = action.get("arguments", [])
        if not arguments:
            continue

        source = extract_source_file(arguments)
        if not source:
            continue

        workspace_source_path = (workspace_root / source).resolve()
        execroot_source_path = (execution_root / source).resolve()
        if workspace_source_path.exists():
            source_path = workspace_source_path
        elif execroot_source_path.exists():
            source_path = execroot_source_path
        else:
            continue

        entry = {
            "directory": str(execution_root),
            "file": str(source_path),
            "command": shlex.join(arguments),
        }
        entries_by_file[entry["file"]] = entry

    return [entries_by_file[k] for k in sorted(entries_by_file)]


def write_compile_commands(entries: Iterable[dict], output_path: Path) -> None:
    output_path.write_text(json.dumps(list(entries), indent=2) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json from Bazel aquery output."
    )
    parser.add_argument(
        "--output",
        default="compile_commands.json",
        help="Output compile_commands.json path (default: workspace root compile_commands.json).",
    )
    parser.add_argument(
        "targets",
        nargs="*",
        default=["//..."],
        help="Bazel target patterns to include (default: //...).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    workspace_root = resolve_workspace()
    execution_root = resolve_execution_root()
    action_expression = build_action_expression(args.targets)

    try:
        aquery_data = run_aquery(action_expression, workspace_root)
    except subprocess.CalledProcessError as err:
        sys.stderr.write(err.stderr)
        return err.returncode

    entries = collect_entries(aquery_data, workspace_root, execution_root)
    if not entries:
        sys.stderr.write("No CppCompile actions found; compile_commands.json not updated.\n")
        return 1

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = workspace_root / output_path
    write_compile_commands(entries, output_path)

    print(f"Wrote {len(entries)} entries to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
