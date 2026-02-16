// ─────────────────────────────────────────────────────────────────────────────
// trt_engine.cpp  –  TensorRT Engine Loader & Inference
// ─────────────────────────────────────────────────────────────────────────────

#include "trt_engine.h"

#include <cuda_runtime_api.h>
#include <NvInfer.h>

#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

namespace golf {

// ─── Logger ─────────────────────────────────────────────────────────────────
void TrtLogger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        std::cerr << "[TRT] " << msg << "\n";
    }
}

// ─── Helpers ────────────────────────────────────────────────────────────────
static size_t volume(const nvinfer1::Dims& d) {
    size_t v = 1;
    for (int i = 0; i < d.nbDims; ++i) {
        v *= static_cast<size_t>(d.d[i]);
    }
    return v;
}

// ─── Destructor ─────────────────────────────────────────────────────────────
TrtEngine::~TrtEngine() {
    release_buffers();
}

// ─── Load ───────────────────────────────────────────────────────────────────
bool TrtEngine::load(const std::string& engine_path) {
    // Read serialized engine
    std::ifstream file(engine_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[TrtEngine] Cannot open " << engine_path << "\n";
        return false;
    }
    const size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> engine_data(file_size);
    if (!file.read(engine_data.data(), file_size)) {
        std::cerr << "[TrtEngine] Failed to read engine file\n";
        return false;
    }
    file.close();

    // Deserialize
    runtime_.reset(nvinfer1::createInferRuntime(logger_));
    if (!runtime_) {
        std::cerr << "[TrtEngine] Failed to create runtime\n";
        return false;
    }

    engine_.reset(runtime_->deserializeCudaEngine(engine_data.data(), file_size));
    if (!engine_) {
        std::cerr << "[TrtEngine] Failed to deserialize engine\n";
        return false;
    }

    context_.reset(engine_->createExecutionContext());
    if (!context_) {
        std::cerr << "[TrtEngine] Failed to create execution context\n";
        return false;
    }

    if (!allocate_buffers()) {
        return false;
    }

    loaded_ = true;
    std::cout << "[TrtEngine] Engine loaded: " << engine_path
              << " (" << file_size / (1024 * 1024) << " MiB)\n";
    return true;
}

// ─── Buffer allocation ──────────────────────────────────────────────────────
bool TrtEngine::allocate_buffers() {
    const int nb = engine_->getNbIOTensors();
    if (nb < 2) {
        std::cerr << "[TrtEngine] Expected at least 2 I/O tensors, got " << nb << "\n";
        return false;
    }

    // Input tensor (index 0)
    const char* input_name = engine_->getIOTensorName(0);
    nvinfer1::Dims in_dims = engine_->getTensorShape(input_name);
    input_c_ = in_dims.d[1];
    input_h_ = in_dims.d[2];
    input_w_ = in_dims.d[3];
    input_size_bytes_ = volume(in_dims) * sizeof(float);

    // Output tensor (index 1)
    const char* output_name = engine_->getIOTensorName(1);
    nvinfer1::Dims out_dims = engine_->getTensorShape(output_name);
    output_length_ = static_cast<int>(volume(out_dims));
    output_size_bytes_ = output_length_ * sizeof(float);

    // Allocate device memory
    if (cudaMalloc(&gpu_buffers_[0], input_size_bytes_) != cudaSuccess) {
        std::cerr << "[TrtEngine] CUDA malloc failed for input\n";
        return false;
    }
    if (cudaMalloc(&gpu_buffers_[1], output_size_bytes_) != cudaSuccess) {
        std::cerr << "[TrtEngine] CUDA malloc failed for output\n";
        return false;
    }

    // Bind tensors to addresses
    context_->setTensorAddress(input_name, gpu_buffers_[0]);
    context_->setTensorAddress(output_name, gpu_buffers_[1]);

    std::cout << "[TrtEngine] Input:  " << input_name
              << " [" << in_dims.d[0] << "x" << input_c_ << "x"
              << input_h_ << "x" << input_w_ << "]\n";
    std::cout << "[TrtEngine] Output: " << output_name
              << " [" << output_length_ << " floats]\n";
    return true;
}

void TrtEngine::release_buffers() {
    if (gpu_buffers_[0]) { cudaFree(gpu_buffers_[0]); gpu_buffers_[0] = nullptr; }
    if (gpu_buffers_[1]) { cudaFree(gpu_buffers_[1]); gpu_buffers_[1] = nullptr; }
}

// ─── Inference ──────────────────────────────────────────────────────────────
bool TrtEngine::infer(const float* input_data, std::vector<float>& output_data) {
    if (!loaded_) {
        std::cerr << "[TrtEngine] Engine not loaded\n";
        return false;
    }

    // Host → Device
    if (cudaMemcpy(gpu_buffers_[0], input_data, input_size_bytes_,
                   cudaMemcpyHostToDevice) != cudaSuccess) {
        std::cerr << "[TrtEngine] H2D copy failed\n";
        return false;
    }

    // Execute
    if (!context_->enqueueV3(nullptr)) {
        std::cerr << "[TrtEngine] enqueueV3 failed\n";
        return false;
    }
    cudaStreamSynchronize(nullptr);

    // Device → Host
    output_data.resize(output_length_);
    if (cudaMemcpy(output_data.data(), gpu_buffers_[1], output_size_bytes_,
                   cudaMemcpyDeviceToHost) != cudaSuccess) {
        std::cerr << "[TrtEngine] D2H copy failed\n";
        return false;
    }
    return true;
}

}  // namespace golf
