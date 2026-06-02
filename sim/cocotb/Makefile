TOPLEVEL_LANG = verilog
SIM ?= icarus
TOPLEVEL = slb_control_polar_encoder
COCOTB_TEST_MODULES = test_slb_control_polar_encoder

REPO_ROOT := $(abspath ../..)
VERILOG_SOURCES = $(REPO_ROOT)/rtl/slb_control_polar_encoder.v
COMPILE_ARGS += -I$(REPO_ROOT)/rtl
SIM_BUILD = $(REPO_ROOT)/build/cocotb
PYTHONPATH := $(REPO_ROOT)/sim/cocotb:$(PYTHONPATH)

include $(shell cocotb-config --makefiles)/Makefile.sim
