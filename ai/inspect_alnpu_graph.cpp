#include <armnn/BackendId.hpp>
#include <armnn/BackendHelper.hpp>
#include <armnn/Descriptors.hpp>
#include <armnn/INetwork.hpp>
#include <armnn/IStrategy.hpp>
#include <armnn/Optional.hpp>
#include <armnn/TypesUtils.hpp>
#include <armnnOnnxParser/IOnnxParser.hpp>

#include <iostream>
#include <string>

namespace {

class SupportStrategy final : public armnn::IStrategy {
public:
    void ExecuteStrategy(
        const armnn::IConnectableLayer* layer,
        const armnn::BaseDescriptor& descriptor,
        const std::vector<armnn::ConstTensor>&,
        const char* name,
        const armnn::LayerBindingId) override {
        std::cout << index_++ << " "
                  << armnn::GetLayerTypeAsCString(layer->GetType())
                  << " name=" << (name ? name : "")
                  << " inputs=" << layer->GetNumInputSlots()
                  << " outputs=" << layer->GetNumOutputSlots() << "\n";
        for (unsigned int i = 0; i < layer->GetNumInputSlots(); ++i) {
            const auto* connection = layer->GetInputSlot(i).GetConnection();
            if (!connection)
                continue;
            const auto& info = connection->GetTensorInfo();
            std::cout << "    input[" << i << "] shape="
                      << info.GetShape() << " type="
                      << static_cast<int>(info.GetDataType())
                      << " quantization=" << info.GetQuantizationScale()
                      << "/" << info.GetQuantizationOffset() << "\n";
        }
        for (unsigned int i = 0; i < layer->GetNumOutputSlots(); ++i) {
            const auto& info = layer->GetOutputSlot(i).GetTensorInfo();
            std::cout << "    output[" << i << "] shape="
                      << info.GetShape() << " type="
                      << static_cast<int>(info.GetDataType())
                      << " quantization=" << info.GetQuantizationScale()
                      << "/" << info.GetQuantizationOffset() << "\n";
        }
        char reason[1024] = {};
        std::string reason_string;
        auto support = armnn::GetILayerSupportByBackendId(
            armnn::BackendId("Alnpu"));
        bool supported = false;
        if (layer->GetType() == armnn::LayerType::Convolution2d) {
            const auto* conv =
                dynamic_cast<const armnn::Convolution2dDescriptor*>(&descriptor);
            if (conv && layer->GetNumInputSlots() >= 2 &&
                layer->GetNumOutputSlots() >= 1) {
                const auto& input = layer->GetInputSlot(0)
                    .GetConnection()->GetTensorInfo();
                const auto& weights = layer->GetInputSlot(1)
                    .GetConnection()->GetTensorInfo();
                const auto& output = layer->GetOutputSlot(0).GetTensorInfo();
                armnn::Optional<armnn::TensorInfo> biases;
                if (layer->GetNumInputSlots() >= 3 && layer->GetInputSlot(2).GetConnection())
                    biases = layer->GetInputSlot(2)
                        .GetConnection()->GetTensorInfo();
                supported = support.IsConvolution2dSupported(
                    input, output, *conv, weights, biases, reason_string);
            }
        } else if (layer->GetType() == armnn::LayerType::Activation) {
            const auto* activation =
                dynamic_cast<const armnn::ActivationDescriptor*>(&descriptor);
            if (activation && layer->GetNumInputSlots() >= 1 &&
                layer->GetNumOutputSlots() >= 1) {
                const auto& input = layer->GetInputSlot(0)
                    .GetConnection()->GetTensorInfo();
                const auto& output = layer->GetOutputSlot(0).GetTensorInfo();
                supported = support.IsActivationSupported(
                    input, output, *activation, reason_string);
            }
        } else if (layer->GetType() == armnn::LayerType::FullyConnected) {
            const auto* fc =
                dynamic_cast<const armnn::FullyConnectedDescriptor*>(&descriptor);
            if (fc && layer->GetNumInputSlots() >= 3 &&
                layer->GetNumOutputSlots() >= 1) {
                const auto& input = layer->GetInputSlot(0)
                    .GetConnection()->GetTensorInfo();
                const auto& weights = layer->GetInputSlot(1)
                    .GetConnection()->GetTensorInfo();
                const auto& biases = layer->GetInputSlot(2)
                    .GetConnection()->GetTensorInfo();
                const auto& output = layer->GetOutputSlot(0).GetTensorInfo();
                supported = support.IsFullyConnectedSupported(
                    input, output, weights, biases, *fc, reason_string);
            }
        }
        std::cout << "    alnpu_supported=" << supported
                  << " reason=" << reason_string << "\n";
    }

private:
    unsigned int index_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " model.onnx\n";
        return 2;
    }
    try {
        auto parser = armnnOnnxParser::IOnnxParser::Create();
        auto network = parser->CreateNetworkFromBinaryFile(argv[1]);
        SupportStrategy strategy;
        network->ExecuteStrategy(strategy);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "inspect error: " << error.what() << "\n";
        return 1;
    }
}
