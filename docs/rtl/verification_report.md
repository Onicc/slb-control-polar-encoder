# RTL Verification Report

Date: 2026-06-02

## Implementation Under Test

- RTL: `rtl/slb_control_polar_encoder.v`
- Generated table include: `rtl/slb_polar_tables.vh`
- Reference model: `sim/cocotb/slb_reference.py`
- Simulator: Icarus Verilog 13.0
- cocotb: 2.0.1
- pytest: 9.0.3
- cocotb-test: 0.2.6

## Scope

The RTL implements one control-information code block per transaction:

```text
c_r + K + E0 + E + rvid -> f_r
```

Multi-block concatenation remains an upper-layer operation:

```text
g = f_0 || f_1 || ... || f_{C-1}
```

## Commands Run

```sh
python3 scripts/generate_rtl_tables.py
iverilog -g2012 -I rtl -o /tmp/slb_encoder.vvp rtl/slb_control_polar_encoder.v
PATH="$PWD/.venv/bin:$PATH" make -C sim/cocotb
.venv/bin/python -m pytest tests/cocotb -q
ctest --test-dir build --output-on-failure
```

## Results

Icarus syntax/elaboration:

```text
PASS
```

cocotb Makefile run:

```text
TESTS=3 PASS=3 FAIL=0 SKIP=0
```

pytest/cocotb-test run:

```text
1 passed
```

C++ regression suite:

```text
100% tests passed, 0 tests failed
```

## cocotb Test Cases

| Test | Purpose | Result |
|---|---|---|
| `golden_and_branch_cases` | Golden cases and representative branch coverage for PC/no-PC, large/small `E`, shortening, and rvid variants | PASS |
| `deterministic_sweep_cases` | 780 valid combinations across `K`, `E0`, `E`, and all four `rvid` values | PASS |
| `invalid_parameter_cases` | Invalid dimensions and `K+nPC>N` rejection | PASS |

## Notes

- The RTL is a clarity-first, single-cycle reference implementation. It is
  appropriate for functional conversion and verification, not yet optimized for
  timing, area, or deep pipelining.
- Input and output bit vectors are LSB-first to match the C++ and Python
  reference models.
- `rtl/slb_polar_tables.vh` should be regenerated whenever the C.1 source table
  changes.
