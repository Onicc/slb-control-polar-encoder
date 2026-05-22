# SLB Control Information Polar Encoder

中文说明: [README.zh-CN.md](README.zh-CN.md)

This repository implements the SLB control-information channel coding path from
T/XS 10001-2025 section 6.2.6.1.5, including the shared Polar-code procedures
referenced by sections 6.2.6.1.2 through 6.2.6.1.4.

The implementation is written in C++17 and packaged as a small CMake library
with focused conformance, boundary, validation, and end-to-end tests.

## What Is Implemented

The module boundary starts after code-block segmentation. Each input block is
already a `c_r(0)..c_r(K_r-1)` sequence. For every control-information block,
the encoder performs:

1. control-information Polar parameter derivation;
2. Table C.1 reliability-sequence filtering for the selected mother code length;
3. information, frozen, and PC bit placement;
4. Polar transform;
5. rate matching with rvid-dependent circular-buffer start;
6. channel interleaving with the three fixed `Iseq` tables from the PDF;
7. code-block concatenation into final `g_k` output.

The data-information-specific section after `6.2.6.1.6.1` is not implemented
because the available main PDF ends at that heading.

## Repository Layout

```text
.
├── CMakeLists.txt
├── include/slb/control_polar_encoder.hpp
├── src/control_polar_encoder.cpp
├── src/polar_reliability_sequence.cpp
├── tests/test_control_polar_encoder.cpp
├── tools/extract_c1_reliability.py
├── docs/control_info_encoder_design.md
├── docs/control_info_encoder_test_report.md
├── docs/table_c1_extraction_report.md
├── SLB技术要求和测试方法.pdf
└── 表 C.1 极化码的可靠度排序序列.pdf
```

## Build Requirements

- CMake 3.16 or newer
- C++17 compiler
- `pdftotext` from Poppler, only needed when regenerating Table C.1

The implementation itself has no third-party runtime dependency.

## Build And Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/control_polar_encoder_tests
```

Expected test result:

```text
100% tests passed, 0 tests failed
Executed 21 tests successfully.
```

## Main API

Include:

```cpp
#include "slb/control_polar_encoder.hpp"
```

Typical usage:

```cpp
#include "slb/control_polar_encoder.hpp"

#include <cstdint>
#include <vector>

int main() {
    using slb::control::Bit;
    using slb::control::ControlBlockInput;

    std::vector<ControlBlockInput> blocks = {
        {
            std::vector<Bit>{1, 0, 1, 1, 0, 0, 1, 0, 1, 0},
            64,  // E0: first transmission rate-matched length
            62,  // E: current transmission rate-matched length
            0,   // rvid
        },
    };

    auto config = slb::control::makeStandardEncoderConfig();
    auto result = slb::control::encodeControlInfo(blocks, config);

    // result.bits is the concatenated g_k output.
    return static_cast<int>(result.bits.empty());
}
```

Primary entry points:

```cpp
slb::control::EncoderConfig slb::control::makeStandardEncoderConfig();

slb::control::EncodeResult slb::control::encodeControlInfo(
    const std::vector<slb::control::ControlBlockInput>& blocks,
    const slb::control::EncoderConfig& config);
```

`makeStandardEncoderConfig()` uses the integrated Table C.1 reliability sequence.
Custom reliability orders can still be injected with `makeEncoderConfig(...)`
for private profiles or focused tests.

## Input Parameters

Each `ControlBlockInput` contains:

| Field | Meaning | Valid values |
|---|---|---|
| `bits` | Code-block bits `c_r` | Non-empty vector of `0` or `1` |
| `firstTransmissionLength` | `E0` | Positive integer |
| `transmissionLength` | Current transmission `E` | Positive integer, no more than `62 * 128` |
| `redundancyVersion` | `rvid` | `0`, `1`, `2`, or `3` |

The module expects code-block segmentation to have already happened before
calling `encodeControlInfo`.

## Output

`EncodeResult` contains:

| Field | Meaning |
|---|---|
| `bits` | Final concatenated `g_k` output, equal to `f_0 || f_1 || ... || f_{C-1}` |
| `blocks` | Per-block debug and conformance information |

Per-block debug data includes:

- `K`, `E0`, `E`, and `rvid`;
- derived `nPC`, `nPCwm`, `K+nPC`, `n`, and `N`;
- selected `T`, temporary frozen bits, information bits, frozen bits, and PC bits;
- rate-match `M`, `k0`, and source indexes;
- intermediate `u`, `d`, `e`, and `f_r` sequences.

This debug structure is intentionally verbose so integration teams can compare
intermediate vectors against external tools or device logs.

## Control-Information Rules

The control-information-specific settings follow section 6.2.6.1.5:

```text
nmax = 10

if K_r < 18 or K_r > 25:
    nPC = 0
else:
    nPC = 3
```

For `18 <= K_r <= 25`, `nPCwm` is derived from the documented threshold:

```text
if E_r - K_r + 3 > 192:
    nPCwm = 1
else:
    nPCwm = 0
```

The implementation uses an overflow-safe equivalent comparison.

## Table C.1 Reliability Sequence

`src/polar_reliability_sequence.cpp` is generated from:

```text
表 C.1 极化码的可靠度排序序列.pdf
```

Regenerate it with:

```sh
python3 tools/extract_c1_reliability.py \
  "表 C.1 极化码的可靠度排序序列.pdf" \
  --cpp-out src/polar_reliability_sequence.cpp \
  --report-out docs/table_c1_extraction_report.md
```

The extraction script parses both raw and layout `pdftotext` output. Generation
fails unless both paths match exactly and the parsed table satisfies:

- 1024 rows;
- 8192 `(W, Q)` pairs;
- `W` covers `0..8191` exactly once;
- `Q` covers `0..8191` exactly once;
- no duplicate `Q` values.

Current extraction sentinels:

```text
first 16 Q values:
0, 1, 2, 4, 8, 16, 32, 3, 5, 64, 9, 6, 17, 10, 18, 128

last 16 Q values:
8179, 8181, 7935, 8182, 8185, 8063, 8186, 8183,
8188, 8187, 8175, 8127, 8190, 8191, 8159, 8189
```

The generated sequence is also checked by the unit tests.

## Validation Behavior

Invalid integration inputs throw `std::invalid_argument`. The module rejects:

- empty block lists;
- empty code blocks;
- non-binary input values;
- `E0 == 0` or `E == 0`;
- invalid `rvid`;
- `K+nPC > N`;
- missing or duplicated reliability indexes below the selected `N`;
- malformed interleaver permutations;
- `E > Col * Rowmax`.

## Test Coverage

The test suite currently has 21 test cases covering:

- fixed interleaver permutation validity;
- mother-code length and PC parameter selection;
- Table C.1 completeness and sentinel values;
- Polar transform known vectors and GF(2) linearity;
- information/frozen/PC bit selection;
- punching and shortening branches;
- rate-match `M`, `k0`, and source indexes;
- channel interleaver row/bank ordering;
- single-block and multi-block end-to-end golden outputs;
- standard Table C.1 end-to-end golden output;
- validation failures;
- maximum interleaver capacity;
- deterministic property sweep over 780 valid parameter scenarios.

See `docs/control_info_encoder_test_report.md` for the full report.

## Commercial Integration Notes

- Use `makeStandardEncoderConfig()` for the standard SLB profile.
- Keep `EncodeResult::blocks` enabled in integration builds until external
  vector matching is complete.
- Use official standard-owner or device-side golden vectors for final
  certification.
- Do not edit `src/polar_reliability_sequence.cpp` by hand; regenerate it with
  `tools/extract_c1_reliability.py`.
- If a future standard revision changes Table C.1, rerun the extractor and keep
  the updated extraction report with the generated source.
