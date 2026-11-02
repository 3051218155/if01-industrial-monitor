#include <armnn/ArmNN.hpp>
#include <armnn/IRuntime.hpp>
#include <armnn/INetwork.hpp>
#include <armnn/Logging.hpp>
#include <armnnALUtils/cma_mem_init.hpp>
#include <armnnOnnxParser/IOnnxParser.hpp>

#include <chrono>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kIterations = 10;

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open " + path);
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " model.onnx input.f32\n";
        return 2;
    }

    try {
        armnn::ConfigureLogging(true, false, armnn::LogSeverity::Error);
        if (cma_mem_init() != 0)
            throw std::runtime_error("cma_mem_init failed");
        auto parser = armnnOnnxParser::IOnnxParser::Create();
        auto network = parser->CreateNetworkFromBinaryFile(argv[1]);

        armnn::IRuntime::CreationOptions runtimeOptions;
        auto runtime = armnn::IRuntime::Create(runtimeOptions);
        armnn::OptimizerOptionsOpaque optimizerOptions;
        optimizerOptions.SetImportEnabled(true);
        optimizerOptions.SetExportEnabled(true);

        // Unlike the production runner, this executable deliberately has no
        // CPU fallback. Success proves ArmNN assigned the whole graph to Alnpu.
        const std::vector<armnn::BackendId> backends{
            armnn::BackendId("Alnpu"),
        };
        auto optimized = armnn::Optimize(
            *network, backends, runtime->GetDeviceSpec(), optimizerOptions);

        armnn::NetworkId networkId = 0;
        std::string error;
        armnn::INetworkProperties properties(
            false,
            armnn::MemorySource::DmaBuf,
            armnn::MemorySource::DmaBuf);
        runtime->LoadNetwork(networkId, std::move(optimized), error, properties);

        const auto inputNames = parser->GetInputTensorNames();
        const auto outputNames = parser->GetOutputTensorNames();
        if (inputNames.size() != 1)
            throw std::runtime_error("expected exactly one model input");

        const auto inputBinding =
            parser->GetNetworkInputBindingInfo(inputNames.front());
        armnn::TensorInfo inputInfo = inputBinding.second;
        auto inputData = readFile(argv[2]);
        if (inputData.size() < inputInfo.GetNumBytes())
            throw std::runtime_error(
                "input file is smaller than model input tensor");
        inputData.resize(inputInfo.GetNumBytes());
        inputInfo.SetConstant(true);
        std::unique_ptr<cma_buffer_t> inputBuffer(
            new cma_buffer_t(static_cast<uint32_t>(inputData.size())));
        if (cma_mem_alloc(inputBuffer.get()) != 0)
            throw std::runtime_error("CMA input allocation failed");
        std::memcpy(inputBuffer->virt_addr, inputData.data(), inputData.size());
        cma_mem_sync_for_device(inputBuffer->phys_addr, inputData.size());

        armnn::InputTensors inputs{
            {inputBinding.first,
             armnn::ConstTensor(inputInfo, inputBuffer->virt_addr)}};

        std::vector<std::vector<std::uint8_t>> outputData;
        std::vector<std::unique_ptr<cma_buffer_t>> outputBuffers;
        outputData.reserve(outputNames.size());
        armnn::OutputTensors outputs;
        for (const std::string& name : outputNames) {
            const auto outputBinding =
                parser->GetNetworkOutputBindingInfo(name);
            const armnn::TensorInfo outputInfo = outputBinding.second;
            outputData.emplace_back(outputInfo.GetNumBytes());
            outputBuffers.emplace_back(
                new cma_buffer_t(outputInfo.GetNumBytes()));
            if (cma_mem_alloc(outputBuffers.back().get()) != 0)
                throw std::runtime_error("CMA output allocation failed");
            outputs.emplace_back(
                outputBinding.first,
                armnn::Tensor(
                    outputInfo, outputBuffers.back()->virt_addr));
        }

        if (runtime->EnqueueWorkload(networkId, inputs, outputs) !=
            armnn::Status::Success) {
            throw std::runtime_error("NPU inference failed");
        }

        const auto start = std::chrono::steady_clock::now();
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
        for (auto& buffer : outputBuffers)
            cma_mem_sync_for_cpu(buffer->phys_addr, buffer->size);
        for (std::size_t index = 0; index < outputBuffers.size(); ++index) {
            std::memcpy(
                outputData[index].data(),
                outputBuffers[index]->virt_addr,
                outputData[index].size());
        }

        std::cout << "backend=Alnpu-strict"
                  << " input_bytes=" << inputInfo.GetNumBytes()
                  << " average_latency_ms=" << averageMs << "\n";
        for (std::size_t outputIndex = 0;
             outputIndex < outputNames.size();
             ++outputIndex) {
            const auto binding =
                parser->GetNetworkOutputBindingInfo(outputNames[outputIndex]);
            const armnn::TensorInfo info = binding.second;
            std::cout << "output=" << outputNames[outputIndex]
                      << " type=" << static_cast<int>(info.GetDataType())
                      << " bytes=" << info.GetNumBytes()
                      << " values=";
            if (info.GetDataType() == armnn::DataType::Float32) {
                const auto* values = reinterpret_cast<const float*>(
                    outputData[outputIndex].data());
                for (unsigned int i = 0; i < info.GetNumElements(); ++i) {
                    if (i)
                        std::cout << ",";
                    std::cout << values[i];
                }
            } else {
                for (std::uint8_t value : outputData[outputIndex])
                    std::cout << static_cast<unsigned>(value) << ",";
            }
            std::cout << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "strict Alnpu error: " << error.what() << "\n";
        return 1;
    }
}
