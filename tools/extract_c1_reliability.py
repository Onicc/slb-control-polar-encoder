#!/usr/bin/env python3
"""Extract table C.1 Polar reliability order from the supplied PDF.

The script intentionally parses both pdftotext raw and layout output and only
generates C++ when the two paths agree exactly. It also verifies that W and Q
each cover 0..8191 exactly once.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


def run_pdftotext(pdf: Path, mode: str) -> str:
    cmd = ["pdftotext"]
    if mode == "layout":
        cmd.append("-layout")
    elif mode == "raw":
        cmd.append("-raw")
    else:
        raise ValueError(f"unsupported pdftotext mode: {mode}")
    cmd.extend([str(pdf), "-"])
    return subprocess.check_output(cmd, text=True)


def parse_pairs(text: str) -> dict[int, int]:
    pairs: dict[int, int] = {}
    for line in text.splitlines():
        numbers = [int(item) for item in re.findall(r"\d+", line)]
        if len(numbers) != 16:
            continue
        if any(value < 0 or value > 8191 for value in numbers):
            continue
        for offset in range(0, 16, 2):
            w_value = numbers[offset]
            q_value = numbers[offset + 1]
            if w_value in pairs and pairs[w_value] != q_value:
                raise ValueError(
                    f"conflicting Q value for W={w_value}: "
                    f"{pairs[w_value]} vs {q_value}"
                )
            pairs[w_value] = q_value
    return pairs


def validate_pairs(pairs: dict[int, int]) -> list[int]:
    expected = set(range(8192))
    w_values = set(pairs)
    q_values = set(pairs.values())
    if w_values != expected:
        missing = sorted(expected - w_values)[:20]
        extra = sorted(w_values - expected)[:20]
        raise ValueError(f"W coverage failed, missing={missing}, extra={extra}")
    if q_values != expected:
        missing = sorted(expected - q_values)[:20]
        extra = sorted(q_values - expected)[:20]
        raise ValueError(f"Q coverage failed, missing={missing}, extra={extra}")
    return [pairs[index] for index in range(8192)]


def format_cpp(sequence: list[int]) -> str:
    rows: list[str] = []
    for start in range(0, len(sequence), 16):
        chunk = sequence[start : start + 16]
        rows.append("        " + ", ".join(f"{value:4d}" for value in chunk) + ",")
    joined_rows = "\n".join(rows)
    return f"""#include \"slb/control_polar_encoder.hpp\"

#include <array>

namespace slb::control {{
namespace {{

constexpr std::array<std::uint16_t, 8192> kPolarReliabilitySequenceC1 = {{
{joined_rows}
}};

}}  // namespace

std::vector<std::size_t> standardReliabilitySequence() {{
    return std::vector<std::size_t>(kPolarReliabilitySequenceC1.begin(),
                                    kPolarReliabilitySequenceC1.end());
}}

EncoderConfig makeStandardEncoderConfig() {{
    return makeEncoderConfig(standardReliabilitySequence());
}}

}}  // namespace slb::control
"""


def format_report(sequence: list[int]) -> str:
    first16 = ", ".join(str(value) for value in sequence[:16])
    last16 = ", ".join(str(value) for value in sequence[-16:])
    return f"""# Table C.1 Extraction Report

Source PDF: `references/表 C.1 极化码的可靠度排序序列.pdf`

Extraction checks:

- raw `pdftotext` path parsed successfully
- layout `pdftotext` path parsed successfully
- raw/layout sequences matched exactly
- parsed rows: 1024
- parsed `(W, Q)` pairs: 8192
- `W` coverage: `0..8191`, no missing values
- `Q` coverage: `0..8191`, no missing values and no duplicates

Sentinel values:

- first 16 Q values: `{first16}`
- last 16 Q values: `{last16}`
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdf", type=Path)
    parser.add_argument("--cpp-out", type=Path, required=True)
    parser.add_argument("--report-out", type=Path, required=True)
    args = parser.parse_args()

    raw_sequence = validate_pairs(parse_pairs(run_pdftotext(args.pdf, "raw")))
    layout_sequence = validate_pairs(parse_pairs(run_pdftotext(args.pdf, "layout")))
    if raw_sequence != layout_sequence:
        raise ValueError("raw/layout extraction mismatch")

    args.cpp_out.write_text(format_cpp(raw_sequence), encoding="utf-8")
    args.report_out.write_text(format_report(raw_sequence), encoding="utf-8")


if __name__ == "__main__":
    main()
