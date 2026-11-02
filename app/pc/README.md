# PC Qt 监视程序

程序使用 Qt 6 Widgets、Network、OpenGLWidgets、Quick 和 Quick3D。源码不依赖固定安装路径。

## 构建

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt6路径>
cmake --build build --config Release
```

构建后，CMake 会把 `assets/` 和 `qml/` 复制到可执行文件目录。

## 串口

默认使用 Modbus RTU `9600 8N1`，可轮询 1 到 6 号设备。1 号设备支持电机启停，其他设备只开放监测功能。

## HTTP

程序默认监听 `8760`：

| 方法 | 路径 | 作用 |
|---|---|---|
| GET | `/api/state` | 查询当前设备与模型结果 |
| POST | `/api/command` | 切换设备、刷新分析或控制 1 号电机 |
| GET | `/api/report` | 生成当前状态报告 |

## 测试

```powershell
cmake --build build --target capture_test serialmodbus_test
```
