# IF-01 Linux userspace tools

CXX ?= g++
ifeq ($(CROSS),1)
  CXX := aarch64-linux-gnu-g++
endif
CXXFLAGS = -Wall -Wextra -O2

TARGETS = pl_regs motor_ctrl modbus_master modbus_slave sensor_monitor fault_ai perf_bench gateway

all: $(TARGETS)

pl_regs: pl_regs.cpp
	$(CXX) $(CXXFLAGS) pl_regs.cpp -o pl_regs

motor_ctrl: motor_ctrl.cpp
	$(CXX) $(CXXFLAGS) motor_ctrl.cpp -o motor_ctrl

perf_bench: perf_bench.cpp
	$(CXX) $(CXXFLAGS) perf_bench.cpp -o perf_bench

modbus_master: modbus_master.cpp
	$(CXX) $(CXXFLAGS) modbus_master.cpp -o modbus_master

modbus_slave: modbus_slave.cpp
	$(CXX) $(CXXFLAGS) modbus_slave.cpp -o modbus_slave -pthread

sensor_monitor: sensor_monitor.cpp
	$(CXX) $(CXXFLAGS) sensor_monitor.cpp -o sensor_monitor

fault_ai: fault_ai.cpp fault_model_weights.h
	$(CXX) $(CXXFLAGS) fault_ai.cpp -o fault_ai

gateway: gateway.cpp
	$(CXX) $(CXXFLAGS) gateway.cpp -o gateway -pthread

clean:
	rm -f $(TARGETS)

.PHONY: all clean
