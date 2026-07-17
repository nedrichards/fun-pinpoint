#!/usr/bin/env python3

"""Aggregate GCC gcov JSON for Pinpoint's authored C sources."""

from __future__ import annotations

import argparse
import gzip
import json
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path)
    return parser.parse_args()


def source_name(document: dict, filename: str, root: Path) -> str | None:
    path = Path(filename)
    if not path.is_absolute():
        path = Path(document["current_working_directory"]) / path
    try:
        relative = path.resolve().relative_to(root)
    except ValueError:
        return None
    name = relative.as_posix()
    if not name.startswith("src/") or not name.endswith(".c"):
        return None
    return name


def ranges(numbers: list[int]) -> str:
    if not numbers:
        return "-"
    result: list[str] = []
    start = previous = numbers[0]
    for number in numbers[1:]:
        if number == previous + 1:
            previous = number
            continue
        result.append(str(start) if start == previous else f"{start}-{previous}")
        start = previous = number
    result.append(str(start) if start == previous else f"{start}-{previous}")
    return ",".join(result)


def percentage(covered: int, total: int) -> str:
    if total == 0:
        return "100.0"
    return f"{covered * 100.0 / total:.1f}"


def abbreviated_ranges(numbers: list[int]) -> str:
    value = ranges(numbers)
    return value if len(value) <= 72 else value[:69] + "..."


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    build_dir = args.build_dir.resolve()
    data_files = sorted(build_dir.rglob("*.gcda"))
    if not data_files:
        print(f"No gcov data found below {build_dir}", file=sys.stderr)
        return 2

    line_counts: dict[str, dict[int, int]] = defaultdict(lambda: defaultdict(int))
    branch_counts: dict[str, dict[tuple, int]] = defaultdict(
        lambda: defaultdict(int)
    )
    function_counts: dict[str, dict[tuple, int]] = defaultdict(
        lambda: defaultdict(int)
    )

    with tempfile.TemporaryDirectory(prefix="pinpoint-gcov-") as temporary:
        for index, data_file in enumerate(data_files):
            object_directory = Path(temporary) / str(index)
            object_directory.mkdir()
            command = [
                "gcov",
                "--json-format",
                "--branch-probabilities",
                "--branch-counts",
                str(data_file),
            ]
            completed = subprocess.run(
                command,
                cwd=object_directory,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            if completed.returncode != 0:
                print(completed.stdout, file=sys.stderr)
                return completed.returncode

            for json_path in object_directory.glob("*.gcov.json.gz"):
                with gzip.open(json_path, "rt", encoding="utf-8") as stream:
                    document = json.load(stream)
                for source in document.get("files", []):
                    name = source_name(document, source["file"], root)
                    if name is None:
                        continue
                    for line in source.get("lines", []):
                        line_number = int(line["line_number"])
                        line_counts[name][line_number] += int(line.get("count", 0))
                        for branch in line.get("branches", []):
                            key = (
                                line_number,
                                int(branch.get("source_block_id", -1)),
                                int(branch.get("destination_block_id", -1)),
                                bool(branch.get("fallthrough", False)),
                                bool(branch.get("throw", False)),
                            )
                            branch_counts[name][key] += int(branch.get("count", 0))
                    for function in source.get("functions", []):
                        key = (
                            function.get("demangled_name", function.get("name", "?")),
                            int(function.get("start_line", 0)),
                            int(function.get("end_line", 0)),
                        )
                        function_counts[name][key] += int(
                            function.get("execution_count", 0)
                        )

    policy = {"excluded_lines": {}, "limits": {}}
    if args.policy:
        with args.policy.open(encoding="utf-8") as stream:
            policy = json.load(stream)

    expected_sources = sorted(
        path.relative_to(root).as_posix() for path in (root / "src").glob("*.c")
    )
    results: dict[str, dict] = {}
    violations: list[str] = []

    for name in expected_sources:
        executable_lines = line_counts.get(name, {})
        exclusions = policy.get("excluded_lines", {}).get(name, {})
        source_lines = (root / name).read_text(encoding="utf-8").splitlines()
        normalized_exclusions: dict[int, str] = {}
        for line_text, entry in exclusions.items():
            line = int(line_text)
            if isinstance(entry, str):
                reason = entry
                expected_source = None
            else:
                reason = entry["reason"]
                expected_source = entry.get("source_contains")
            normalized_exclusions[line] = reason
            if line not in executable_lines:
                violations.append(f"{name}:{line}: exclusion is not executable")
            elif executable_lines[line] != 0:
                violations.append(f"{name}:{line}: excluded line is now covered")
            if (expected_source is not None and
                (line > len(source_lines) or
                 expected_source not in source_lines[line - 1])):
                violations.append(f"{name}:{line}: excluded source text changed")
        exclusions = normalized_exclusions
        adjusted_lines = {
            line: count
            for line, count in executable_lines.items()
            if line not in exclusions
        }
        functions = function_counts.get(name, {})
        branches = {
            key: count
            for key, count in branch_counts.get(name, {}).items()
            if key[0] not in exclusions
        }
        uncovered_lines = sorted(
            line for line, count in adjusted_lines.items() if count == 0
        )
        uncovered_functions = sorted(
            f"{key[0]}:{key[1]}" for key, count in functions.items() if count == 0
        )
        uncovered_branches = sum(count == 0 for count in branches.values())
        results[name] = {
            "lines": {
                "covered": len(adjusted_lines) - len(uncovered_lines),
                "total": len(adjusted_lines),
                "uncovered": uncovered_lines,
            },
            "functions": {
                "covered": len(functions) - len(uncovered_functions),
                "total": len(functions),
                "uncovered": uncovered_functions,
            },
            "branches": {
                "covered": len(branches) - uncovered_branches,
                "total": len(branches),
                "uncovered": uncovered_branches,
            },
            "excluded_lines": {str(line): reason for line, reason in exclusions.items()},
        }

        limits = policy.get("limits", {}).get(name)
        if limits is None and policy.get("limits"):
            violations.append(f"{name}: no reviewed coverage limits")
            continue
        if limits:
            actual = {
                "uncovered_lines": len(uncovered_lines),
                "uncovered_functions": len(uncovered_functions),
                "uncovered_branches": uncovered_branches,
            }
            for metric, maximum in limits.items():
                if actual[metric] > maximum:
                    violations.append(
                        f"{name}: {metric} is {actual[metric]}, allowed {maximum}"
                    )

    output = {
        "format": 1,
        "generator": "gcc-gcov-json",
        "sources": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as stream:
        json.dump(output, stream, indent=2, sort_keys=True)
        stream.write("\n")

    totals = {
        category: {
            "covered": sum(result[category]["covered"] for result in results.values()),
            "total": sum(result[category]["total"] for result in results.values()),
        }
        for category in ("lines", "functions", "branches")
    }
    print("Pinpoint GCC coverage")
    print("source                              lines       funcs      branches  uncovered lines")
    for name, result in results.items():
        line = result["lines"]
        function = result["functions"]
        branch = result["branches"]
        print(
            f"{name:35} "
            f"{line['covered']:4}/{line['total']:<4} "
            f"{percentage(line['covered'], line['total']):>5}%  "
            f"{function['covered']:3}/{function['total']:<3}  "
            f"{branch['covered']:4}/{branch['total']:<4}  "
            f"{abbreviated_ranges(line['uncovered'])}"
        )
    print()
    for category, total in totals.items():
        print(
            f"{category.capitalize():9}: {total['covered']}/{total['total']} "
            f"({percentage(total['covered'], total['total'])}%)"
        )
    print(f"JSON report: {args.output}")

    if violations:
        print("\nCoverage policy violations:", file=sys.stderr)
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
