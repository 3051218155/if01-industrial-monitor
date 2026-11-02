# 香橙派语音主程序

`main.py` 负责唤醒词检测、录音、语音识别、意图解析、远程请求和语音播报。在线服务不可用时，可切换到规则解析和键盘模式。

## 启动

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python main.py --pc <PC地址> --gateway <板端地址>
```

## 键盘模式

```bash
python main.py --keyboard --pc <PC地址>
```

## 配置

配置位于 `config.yaml`。部署前需要填写：

- 语音服务 AppID、API Key 和 Secret Key
- 在线意图解析服务地址与 Key
- 板端网关和 PC 监视程序地址
- `arecord -l`、`aplay -l` 查到的音频设备

## 自检

```bash
python doctor.py
```

自检会检查 Python 依赖、音频设备、网关、PC 接口、配置项和本地目录。
