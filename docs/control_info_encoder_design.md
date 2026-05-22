# SLB Control Information Polar Encoder Design

This module implements the control-information transmission path described by
T/XS 10001-2025 section 6.2.6.1.5 and the common Polar-code procedures it
references in sections 6.2.6.1.2 through 6.2.6.1.4.

## Scope

The module boundary starts after code-block segmentation. Each input block is
already a `c_r(0)..c_r(K_r-1)` sequence. The implementation performs:

1. control-information Polar parameter derivation;
2. reliability-sequence filtering for the selected mother code length;
3. information/frozen/PC bit placement;
4. Polar transform;
5. rate matching with rvid-dependent circular-buffer start;
6. channel interleaving using the three fixed `Iseq` tables in the PDF;
7. code-block concatenation.

The standard Table C.1 reliability sequence is integrated in
`src/polar_reliability_sequence.cpp`. It was generated from
`references/表 C.1 极化码的可靠度排序序列.pdf` by
`tools/extract_c1_reliability.py`, which
cross-checks raw and layout `pdftotext` extraction paths and verifies that both
`W` and `Q` cover `0..8191` exactly once.

Production callers can use `makeStandardEncoderConfig()`. `EncoderConfig` still
accepts an injected reliability sequence for private profiles and focused tests.
The helper `naturalReliabilitySequence()` is deterministic scaffolding only.

## Main API

```cpp
using slb::control::ControlBlockInput;
using slb::control::EncoderConfig;
using slb::control::EncodeResult;

EncodeResult encodeControlInfo(
    const std::vector<ControlBlockInput>& blocks,
    const EncoderConfig& config);

EncoderConfig makeStandardEncoderConfig();
```

Each `ControlBlockInput` contains:

- `bits`: code-block bits `c_r`;
- `firstTransmissionLength`: `E0`;
- `transmissionLength`: current transmission `E`;
- `redundancyVersion`: `rvid` in `{0,1,2,3}`.

`EncodeResult::bits` is the concatenated `g_k` output. `EncodeResult::blocks`
contains debug and conformance data: `N`, `nPC`, `nPCwm`, `T`, `QI`, `QF`,
`QPC`, `M`, `k0`, `u`, `d`, `e`, and `f_r`.

## Validation Policy

The implementation rejects invalid commercial-integration inputs with
`std::invalid_argument`, including:

- empty block list;
- non-binary input bits;
- `E0 == 0` or `E == 0`;
- invalid `rvid`;
- `K+nPC > N`;
- missing or duplicated reliability indexes below the selected `N`;
- malformed interleaver permutations;
- `E > Col * Rowmax`.

## Interleaver Interpretation

The figure on PDF page 59 is implemented as follows:

1. Write input `e` into rows of 62 bits.
2. Select `Iseq[row_id mod 3]` and permute the 62 columns in that row.
3. Split each row into two 31-bit banks.
4. Output all used rows of bank 0 first, then all used rows of bank 1, skipping
   empty cells in the final partial row.
