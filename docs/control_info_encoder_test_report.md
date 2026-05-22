# SLB Control Information Polar Encoder Test Report

Date: 2026-05-22

## Build Under Test

- Module: `slb_control_polar_encoder`
- Language standard: C++17
- Build system: CMake
- Compiler observed: Apple clang 17.0.0
- Source standard reference: `SLB技术要求和测试方法.pdf`, section 6.2.6.1.5 and referenced common Polar-code sections 6.2.6.1.2 to 6.2.6.1.4

## Important Conformance Note

The workspace now includes `表 C.1 极化码的可靠度排序序列.pdf`. Table C.1 was
converted into `src/polar_reliability_sequence.cpp` using
`tools/extract_c1_reliability.py`. The extraction script parses both raw and
layout `pdftotext` output, requires both paths to match exactly, and verifies
8192 `(W, Q)` pairs with complete `0..8191` coverage and no duplicate `Q`
values. The extraction details are recorded in
`docs/table_c1_extraction_report.md`.

External conformance vectors from the standard owner are still recommended for
certification.

## Test Environment

Commands executed:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/control_polar_encoder_tests
```

Result:

```text
100% tests passed, 0 tests failed out of 1
Executed 21 tests successfully.
```

## Test Cases

| ID | Test case | Coverage |
|---|---|---|
| TC-001 | default interleaver sequences are permutations | Validates each fixed `Iseq` is a permutation of `0..61`. |
| TC-002 | mother code length and PC parameter selection | Covers `nmin`, `nmax`, `nPC`, and `nPCwm` threshold logic. |
| TC-003 | reliability sequence filtering and validation | Covers current-`N` filtering, duplicate detection, and missing-index rejection. |
| TC-004 | standard C.1 reliability sequence integrity | Verifies 8192 values, `0..8191` permutation coverage, first/last sentinel values, and `Q_1024` derivation. |
| TC-005 | Polar transform small vectors | Checks known `N=2` and `N=4` transform outputs. |
| TC-006 | Polar transform linearity | Verifies GF(2) linearity for `N=32`, `64`, and `128`. |
| TC-007 | bit selection without PC using natural reliability | Covers `E0 >= N` and highest-reliability information-bit selection. |
| TC-008 | bit selection formula branches | Exercises the documented `T` formulas for punching and shortening branches. |
| TC-009 | shortening temporary frozen bits | Verifies `[E0:N-1]` temporary freezing behavior. |
| TC-010 | PC bit selection | Covers low-reliability PC selection and row-weight-based PC selection. |
| TC-011 | Polar input construction with PC bits | Verifies frozen bits, PC positions, and information-bit consumption. |
| TC-012 | rate match plan and output | Covers `M`, `k0`, rvid=2, and circular-buffer output selection. |
| TC-013 | rate match shortening plan | Covers shortening/CC `M=min(E0,N)` behavior. |
| TC-014 | channel interleaver index map | Verifies row/bank output ordering and permutation property. |
| TC-015 | channel interleaver bits | Verifies bit output follows the generated index map. |
| TC-016 | end-to-end single block golden | Checks complete single-block output and `g=f_0`. |
| TC-017 | end-to-end multi-block golden | Checks complete two-block output, PC/rvid branch, and `g=f_0||f_1`. |
| TC-018 | end-to-end with standard C.1 reliability | Checks production reliability table integration, information positions, PC positions, and full output golden value. |
| TC-019 | validation failures | Covers empty inputs, non-bit input, invalid rvid, interleaver overflow, and missing reliability table entries. |
| TC-020 | interleaver capacity boundary | Exercises maximum supported `E=62*128=7936`. |
| TC-021 | deterministic property sweep | Covers 780 valid parameter scenarios across `K`, `E0`, `E`, and all four rvid values; also confirms 5 invalid parameter combinations are rejected. |

## Residual Risks

- External conformance vectors from the standard owner are still required.
- The available PDF ends at the `6.2.6.1.6.1` heading, so data-information-specific rules outside control information are intentionally out of scope.

## Acceptance

The implementation is ready for integration testing as a control-information
encoder library using the integrated Table C.1 reliability sequence, subject to
validation against official external golden vectors.
