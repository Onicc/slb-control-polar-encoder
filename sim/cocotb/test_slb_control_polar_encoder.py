import random

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge

from slb_reference import bits_to_int, encode_control_block


async def reset_dut(dut):
    dut.rst_n.value = 0
    dut.start_i.value = 0
    dut.k_i.value = 0
    dut.e0_i.value = 0
    dut.e_i.value = 0
    dut.rvid_i.value = 0
    dut.c_bits_i.value = 0
    await RisingEdge(dut.clk)
    await RisingEdge(dut.clk)
    dut.rst_n.value = 1
    await RisingEdge(dut.clk)


async def run_case(dut, c_bits, e0, e, rvid):
    expected = encode_control_block(c_bits, e0, e, rvid)
    dut.k_i.value = len(c_bits)
    dut.e0_i.value = e0
    dut.e_i.value = e
    dut.rvid_i.value = rvid
    dut.c_bits_i.value = bits_to_int(c_bits)
    dut.start_i.value = 1
    await RisingEdge(dut.clk)
    dut.start_i.value = 0
    await RisingEdge(dut.clk)

    assert int(dut.done_o.value) == 1
    assert int(dut.error_o.value) == 0
    assert int(dut.out_len_o.value) == e
    assert int(dut.n_len_o.value) == expected.n_len
    assert int(dut.n_exp_o.value) == expected.n_exp
    assert int(dut.npc_o.value) == expected.npc
    assert int(dut.npc_wm_o.value) == expected.npc_wm
    assert int(dut.m_o.value) == expected.m
    assert int(dut.k0_o.value) == expected.k0

    actual_value = int(dut.f_bits_o.value)
    expected_value = bits_to_int(expected.bits)
    mask = (1 << e) - 1
    assert (actual_value & mask) == expected_value, (
        f"Mismatch for K={len(c_bits)} E0={e0} E={e} rvid={rvid}: "
        f"actual=0x{actual_value & mask:x} expected=0x{expected_value:x}"
    )
    await RisingEdge(dut.clk)
    assert int(dut.done_o.value) == 0


@cocotb.test()
async def golden_and_branch_cases(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset_dut(dut)

    cases = [
        ([((i * 3 + 1) & 1) for i in range(10)], 64, 62, 0),
        ([(((i + 1) * 3 + 1) & 1) for i in range(18)], 64, 124, 2),
        ([i & 1 for i in range(18)], 64, 208, 3),
        ([1 if i % 3 == 0 else 0 for i in range(25)], 256, 511, 1),
        ([1 if i % 5 in (0, 2) else 0 for i in range(40)], 80, 80, 0),
        ([1 if i % 7 in (1, 6) else 0 for i in range(26)], 5000, 62, 2),
    ]
    for case in cases:
        await run_case(dut, *case)


@cocotb.test()
async def deterministic_sweep_cases(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset_dut(dut)

    rng = random.Random(20260602)
    k_values = [1, 10, 17, 18, 21, 25, 26, 40]
    e0_values = [32, 64, 128, 256, 1024]
    e_values = [31, 62, 124, 255, 511]
    tested = 0
    for k in k_values:
        for e0 in e0_values:
            for e in e_values:
                c_bits = [rng.randrange(2) for _ in range(k)]
                for rvid in range(4):
                    try:
                        encode_control_block(c_bits, e0, e, rvid)
                    except ValueError:
                        continue
                    await run_case(dut, c_bits, e0, e, rvid)
                    tested += 1
    assert tested == 780


@cocotb.test()
async def invalid_parameter_cases(dut):
    cocotb.start_soon(Clock(dut.clk, 10, unit="ns").start())
    await reset_dut(dut)

    invalid_cases = [
        ([], 64, 62, 0),
        ([1] * 40, 32, 62, 0),
        ([1] * 10, 0, 62, 0),
        ([1] * 10, 64, 0, 0),
    ]
    for c_bits, e0, e, rvid in invalid_cases:
        dut.k_i.value = len(c_bits)
        dut.e0_i.value = e0
        dut.e_i.value = e
        dut.rvid_i.value = rvid
        dut.c_bits_i.value = bits_to_int(c_bits)
        dut.start_i.value = 1
        await RisingEdge(dut.clk)
        dut.start_i.value = 0
        await RisingEdge(dut.clk)
        assert int(dut.done_o.value) == 1
        assert int(dut.error_o.value) == 1
        await RisingEdge(dut.clk)
