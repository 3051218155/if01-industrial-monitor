# IF-01 多设备状态监测系统

本仓库整理的是设备状态监测项目的最终源码，覆盖 FPGA 实时采集与控制、Linux 网关、PC 端 Qt 监视程序、NPU 工况识别以及香橙派语音控制。

## 目录

| 目录 | 内容 |
|---|---|
| `firmware/pl` | AXI-Lite、PWM、RS485、MPU6050、FIR 和特征提取 RTL |
| `firmware/linux` | 板端采集、Modbus、电机控制和 TCP 网关 |
| `app/pc` | Qt 6 多设备监视界面与本地 HTTP 接口 |
| `ai` | 模型训练、量化、NPU 运行器与已导出的模型文件 |
| `voice` | 香橙派语音识别、意图解析和远程控制 |
| `deploy` | SSH 部署脚本与 systemd 服务文件 |
| `docs` | 架构、接口和部署说明 |

## 数据链路

```text
MPU6050 / RS485 / CAN
          |
       FPGA PL
          |
   AXI-Lite / Linux
          |
   Modbus + TCP gateway
          |
 Qt HMI / HTTP 8760 / NPU
          |
      Orange Pi voice
```

## 构建

### PC 端

需要 Qt 6，并安装 Widgets、Network、OpenGLWidgets、Quick、QuickWidgets 和 Quick3D 组件。

```powershell
cmake -S app/pc -B app/pc/build -DCMAKE_PREFIX_PATH=<Qt6路径>
cmake --build app/pc/build --config Release
```

构建后，`assets/` 和 `qml/` 会复制到可执行文件旁。也可以直接打开 `app/pc/CMakeLists.txt` 用 Qt Creator 构建。

### Linux 板端

```bash
cd firmware/linux
make
```

产物包括 `gateway`、`modbus_master`、`modbus_slave`、`motor_ctrl`、`pl_regs`、`sensor_monitor`、`fault_ai` 和 `perf_bench`。

### 香橙派

```bash
cd voice
python3 -m venv .venv
. .venv/bin/activate
pip install -r robot/requirements.txt
pip install -r orange_pi_mcp/requirements.txt
```

部署和启动参数见 [voice/README.md](voice/README.md)。

## 板端部署

脚本通过 SSH 工作，板子地址和账号可以用参数或环境变量指定：

```powershell
$env:FPGA_BOARD_HOST = "192.168.137.100"
$env:FPGA_BOARD_USER = "root"
python deploy/deploy_ps_to_board.py
python deploy/deploy_npu_runtime.py
```

脚本运行时会提示输入板端密码；也可以在运行前设置本机环境变量 `FPGA_BOARD_PASSWORD`。密码只保存在本机，不写入仓库。

## 工程说明

- FPGA 顶层模块为 `fpga_if01_top`，AXI-Lite 基地址为 `0x80000000`。
- 板端网关默认监听 TCP `9000`，PC 端接口默认监听 HTTP `8760`。
- 模型和输入样例位于 `ai/model`，模型指标保存在同目录的 `metrics.json`。
- 串口日志、构建目录、密钥、本地 IP 配置和厂商工具链不进入仓库。

寄存器与总线说明见 [docs/interfaces.md](docs/interfaces.md)，系统组成见 [docs/architecture.md](docs/architecture.md)。
