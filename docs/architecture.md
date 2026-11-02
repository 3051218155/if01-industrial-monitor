# 系统组成

## FPGA

PL 负责采样时序、FIR 滤波、RMS/峰值和测点特征、双路 PWM，以及 RS485 Modbus 帧处理。CPU 通过 `0x80000000` 开始的 AXI-Lite 窗口访问寄存器和状态。

主要模块：

- `fpga_if01_top.v`：顶层与 AXI-Lite 连接
- `fpga_if01_reg_map.v`：寄存器定义
- `fpga_sensor_core.v`：MPU6050 采样与滤波
- `fpga_feature_extract.v`：振动特征与故障部位
- `fpga_modbus_rtu_parser.v`：RS485 Modbus RTU 帧处理
- `fpga_dual_motor.v`：PWM 与方向控制

## Linux

`gateway` 从 PL 寄存器读取运行状态，并通过 RS485 轮询外部设备。PC 端通过串口或 TCP `9000` 获取统一 JSON。`modbus_slave` 提供板端从站和 CSV 记录服务。

## PC

Qt 程序负责设备切换、实时曲线、告警、报告、3D 视图和香橙派接口。默认 HTTP 端口为 `8760`：

```text
GET  /api/state
POST /api/command
GET  /api/report
```

## 工况识别

模型输入为 8 个连续采样组成的时间窗口。当前导出的 9x9 UINT8 图由 `generic_npu_run_strict` 交给 `Alnpu` 执行，Python 层负责输入整理、加锁和后处理。

## 语音控制

香橙派程序采集语音并解析为查询、切换画面或电机控制命令。查询请求走 TCP `9000` 或 PC 的 HTTP `8760`。未配置在线语音服务时，可以使用键盘模式和本地规则解析。
