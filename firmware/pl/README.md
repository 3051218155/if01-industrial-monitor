# IF-01 FPGA

顶层模块为 `rtl/fpga_if01_top.v`，时钟和复位接到板级 PL 时钟与 `pl_resetn`。AXI-Lite 从机使用 `0x80000000` 基地址，范围 4 KB。

## 源码

| 文件 | 说明 |
|---|---|
| `rtl/fpga_if01_top.v` | 顶层 |
| `rtl/fpga_axi_lite_slave.v` | AXI4-Lite 从机 |
| `rtl/fpga_if01_reg_map.v` | 寄存器和状态 |
| `rtl/fpga_sensor_core.v` | 采样与滤波器 |
| `rtl/fpga_feature_extract.v` | 特征计算 |
| `rtl/fpga_rs485_uart.v` | RS485 收发 |
| `rtl/fpga_modbus_rtu_parser.v` | Modbus RTU 帧处理 |
| `rtl/fpga_dual_motor.v` | 双路 PWM |
| `rtl/fpga_perf_monitor.v` | 性能计数 |
| `constraints/fpga_if01_motor.adc` | 板级管脚约束 |

## 集成

1. 在 Anlogic FPSoC 工程中加入 `rtl` 下的 Verilog。
2. 将 `fpga_if01_top` 加入 Block Design。
3. 连接 PS AXI GP Master 到 `0x80000000`。
4. 导入 `constraints/fpga_if01_motor.adc`。
5. 综合、布局布线并生成位流。

## 自检

安装 Icarus Verilog 后可以运行：

```bash
iverilog -o tb_fpga rtl/*.v tb/tb_fpag_if01.v
vvp tb_fpga
```

板端寄存器读取：

```bash
PL_REG_BASE=0x80000000 ./pl_regs read 0x08
PL_REG_BASE=0x80000000 ./pl_regs read 0x30
```
