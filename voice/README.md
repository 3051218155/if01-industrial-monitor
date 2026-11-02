# 香橙派语音控制

语音程序把麦克风输入转换为设备查询、画面切换或电机控制请求。默认链路为：

```text
麦克风 -> ASR -> 意图解析 -> 网关/PC 接口 -> TTS 播报
```

## 目录

| 目录 | 内容 |
|---|---|
| `robot/` | 语音状态机、ASR、TTS、意图解析和键盘模式 |
| `orange_pi_mcp/` | PC HTTP、TCP 网关和本地语音服务 |

## 安装

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r robot/requirements.txt
pip install -r orange_pi_mcp/requirements.txt
```

系统还需要 `arecord` 和 `aplay`，可选安装 `sox`。

## 配置

主要配置位于 `robot/config.yaml`：

- `llm.api_key`：在线意图解析服务密钥
- `baidu.app_id`、`baidu.api_key`、`baidu.secret_key`：语音服务密钥
- `gateway.host`：板端网关地址
- `pc_display.host`：PC 监视程序地址
- `asr.device`、`tts.device`：录音和播放设备

仓库不包含密钥，部署时需要在本机填写。

## 启动

```bash
python main.py --pc <PC地址> --gateway <板端地址>
```

无麦克风时使用键盘模式：

```bash
python main.py --keyboard --pc <PC地址>
```

单独启动语音服务：

```bash
cd orange_pi_mcp
python3 voice_controller.py --serve --host 0.0.0.0 --port 8780
```
