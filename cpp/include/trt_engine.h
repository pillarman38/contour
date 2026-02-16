#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// trt_engine.h  –  TensorRT Engine Loader & Inference Wrapper
// ─────────────────────────────────────────────────────────────────────────────

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <memory>
#include <string>
#include <vector>

namespace golf {

// Custom deleter for TensorRT objects
struct TrtDeleter {
    template <typename T>
    void operator()(T* obj) const {
        delete obj;
    }
};

// Logger forwarded to stderr
class TrtLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
};

// ─── TensorRT Engine ────────────────────────────────────────────────────────
class TrtEngine {
public:
    TrtEngine() = default;
    ~TrtEngine();

    TrtEngine(const TrtEngine&) = delete;
    TrtEngine& operator=(const TrtEngine&) = delete;

    /// Load a serialized TensorRT engine from disk.
    bool load(const std::string& engine_path);

    /// Run inference on pre-processed input (NCHW, float32, 0-1).
    /// @param input_data   pointer to host input (1×3×H×W floats)
    /// @param output_data  resized by the call to hold raw network output
    /// @return true on success
    bool infer(const float* input_data, std::vector<float>& output_data);

    int input_h() const { return input_h_; }
    int input_w() const { return input_w_; }
    int input_c() const { return input_c_; }

private:
    bool allocate_buffers();
    void release_buffers();

    TrtLogger logger_;
    std::unique_ptr<nvinfer1::IRuntime, TrtDeleter> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine, TrtDeleter> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext, TrtDeleter> context_;

    // GPU buffer pointers (index 0 = input, 1 = output)
    void* gpu_buffers_[2]{nullptr, nullptr};
    size_t input_size_bytes_ = 0;
    size_t output_size_bytes_ = 0;
    int output_length_ = 0;

    int input_c_ = 3;
    int input_h_ = 640;
    int input_w_ = 640;

    bool loaded_ = false;
};

}  // namespace golf
