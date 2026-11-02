# 工况识别

本目录包含六设备工况模型的训练、量化、导出和板端运行代码。

## 模型

`model/` 中提供：

- `condition_runtime_npu16_9x9_qdq_inferred.onnx`：板端 HARDNPU 使用的 UINT8 图
- `condition_mlp_float.onnx`：浮点参考模型
- `condition_mlp_qdq_uint8.onnx`：量化模型
- `runtime_preprocess.json`：运行期归一化参数
- `condition_npu_samples/`：五类输入样例
- `metrics.json`：训练与量化指标

输入为 8 个连续采样点，每个点包含 RMS、峰值、温度、电流和 PWM。模型输出停机、空载、正常带载、过载和异常五类。

## PC 推理

```powershell
python gateway_condition_infer.py --device-id 1
python run_condition_onnx.py --help
```

PC 端使用 ONNX Runtime，适合检查网关数据到模型接口，不代表板端 NPU 耗时。

## 板端部署

```powershell
python ..\deploy\deploy_npu_runtime.py
```

部署完成后，板端目录为 `/root/ai/condition/`。严格运行器只允许 `Alnpu`，不会退回 CPU：

```bash
python3 /root/ai/condition/npu_condition_infer.py \
  --samples /root/ai/condition/condition_npu_samples/normal.json
```

## 重新训练

```powershell
conda env create -f environment.yml
python train_runtime_condition.py
python build_npu16_9x9_model.py
```

训练数据和导出参数需要与目标板寄存器定义保持一致。
