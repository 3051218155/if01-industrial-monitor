#include <armnn/BackendId.hpp>
#include <armnn/IRuntime.hpp>
#include <armnn/Logging.hpp>
#include <armnn/Tensor.hpp>
#include <armnnOnnxParser/IOnnxParser.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> ReadFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open " + path);
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " model.onnx input.f32\n";
        return 2;
    }

    try {
        armnn::ConfigureLogging(true, false, armnn::LogSeverity::Error);
        auto parser = armnnOnnxParser::IOnnxParser::Create();
        auto network = parser->CreateNetworkFromBinaryFile(argv[1]);

        armnn::IRuntime::CreationOptions runtimeOptions;
        auto runtime = armnn::IRuntime::Create(runtimeOptions);

        armnn::OptimizerOptionsOpaque optimizerOptions;
        optimizerOptions.SetImportEnabled(true);
        optimizerOptions.SetExportEnabled(true);
        const std::vector<armnn::BackendId> backends{
            armnn::BackendId("Alnpu"),
            armnn::BackendId("CpuAcc"),
            armnn::BackendId("CpuRef"),
        };
        auto optimized = armnn::Optimize(
            *network, backends, runtime->GetDeviceSpec(), optimizerOptions);

        armnn::NetworkId networkId = 0;
        std::string error;
        armnn::INetworkProperties properties(
            false, armnn::MemorySource::Malloc, armnn::MemorySource::Malloc);
        runtime->LoadNetwork(networkId, std::move(optimized), error, properties);

        const auto inputNames = parser->GetInputTensorNames();
        const auto outputNames = parser->GetOutputTensorNames();
        if (inputNames.size() != 1) {
            throw std::runtime_error("expected exactly one model input");
        }

        const auto inputBinding =
            parser->GetNetworkInputBindingInfo(inputNames.front());
        armnn::TensorInfo inputInfo = inputBinding.second;
        auto inputData = ReadFile(argv[2]);
        if (inputData.size() < inputInfo.GetNumBytes()) {
            throw std::runtime_error(
                "input file is smaller than model input tensor");
        }
        inputData.resize(inputInfo.GetNumBytes());
        inputInfo.SetConstant(true);

        armnn::InputTensors inputs{
            {inputBinding.first,
             armnn::ConstTensor(inputInfo, inputData.data())}};

        std::vector<std::vector<std::uint8_t>> outputData;
        outputData.reserve(outputNames.size());
        armnn::OutputTensors outputs;
        for (const std::string& name : outputNames) {
            const auto outputBinding =
                parser->GetNetworkOutputBindingInfo(name);
            const armnn::TensorInfo outputInfo = outputBinding.second;
            outputData.emplace_back(outputInfo.GetNumBytes());
            outputs.emplace_back(
                outputBinding.first,
                armnn::Tensor(
                    outputInfo, outputData.back().data()));
        }

        if (runtime->EnqueueWorkload(networkId, inputs, outputs) !=
            armnn::Status::Success) {
            throw std::runtime_error("NPU inference failed");
        }

        const auto start = std::chrono::steady_clock::now();
        constexpr int kIterations = 10;
        for (int i = 0; i < kIterations; ++i) {
            if (runtime->EnqueueWorkload(networkId, inputs, outputs) !=
                armnn::Status::Success) {
                throw std::runtime_error("NPU inference failed");
            }
        }
        const auto stop = std::chrono::steady_clock::now();
        const double averageMs =
            std::chrono::duration<double, std::milli>(stop - start).count() /
            kIterations;

        std::cout << "backend=Alnpu"
                  << " input_bytes=" << inputInfo.GetNumBytes()
                  << " average_latency_ms=" << averageMs << "\n";

        for (std::size_t outputIndex = 0;
             outputIndex < outputNames.size(); ++outputIndex) {
            const auto binding =
                parser->GetNetworkOutputBindingInfo(outputNames[outputIndex]);
            const armnn::TensorInfo info = binding.second;
            std::cout << "output=" << outputNames[outputIndex]
                      << " type=" << static_cast<int>(info.GetDataType())
                      << " bytes=" << info.GetNumBytes() << " values=";
            if (info.GetDataType() == armnn::DataType::Float32) {
                const auto* values =
                    reinterpret_cast<const float*>(outputData[outputIndex].data());
                const std::size_t count =
                    info.GetNumBytes() / sizeof(float);
                int best = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    std::cout << values[i] << (i + 1 == count ? "" : ",");
                    if (values[i] > values[best]) {
                        best = static_cast<int>(i);
                    }
                }
                std::cout << " argmax=" << best;
            }
            std::cout << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
