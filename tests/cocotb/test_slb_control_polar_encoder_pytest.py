from pathlib import Path

from cocotb_test.simulator import run


def test_slb_control_polar_encoder_icarus():
    repo = Path(__file__).resolve().parents[2]
    run(
        simulator="icarus",
        toplevel_lang="verilog",
        verilog_sources=[str(repo / "rtl" / "slb_control_polar_encoder.v")],
        includes=[str(repo / "rtl")],
        toplevel="slb_control_polar_encoder",
        module="test_slb_control_polar_encoder",
        python_search=[str(repo / "sim" / "cocotb")],
        sim_build=str(repo / "build" / "cocotb_pytest"),
        waves=False,
    )
