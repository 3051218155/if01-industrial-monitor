# IF-01 Linux 程序

## 构建

```bash
make
```

主要程序：

| 程序 | 作用 |
|---|---|
| `gateway` | 汇总 PL、RS485 与设备状态，提供 TCP 9000 |
| `modbus_master` | 轮询 RS485 从站 |
| `modbus_slave` | 提供板端从站与 CSV 记录 |
| `motor_ctrl` | 控制 PL PWM 与方向 |
| `pl_regs` | 读写 PL 寄存器 |
| `sensor_monitor` | 查看 MPU6050 与特征 |
| `fault_ai` | 本地阈值判级 |
| `perf_bench` | AXI 与采样性能测试 |

## 运行

板端可使用以下 systemd 配置：

```bash
sudo cp deploy/fpga-modbus-slave.service /etc/systemd/system/
sudo cp deploy/if01-gateway.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now fpga-modbus-slave.service if01-gateway.service
```

手动排查：

```bash
./pl_regs read 0x08
./gateway
```
