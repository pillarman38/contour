#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// frame_pipeline.h  –  OpenCV Frame Capture & Pre-processing
// ─────────────────────────────────────────────────────────────────────────────

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace golf {

/// Represents a single detection from the network output.
struct Detection {
    int class_id;           // 0 = golf_ball, 1 = putter
    float confidence;
    float x1, y1, x2, y2;  // bounding box (pixel coords in original frame)

    float cx() const { return (x1 + x2) * 0.5f; }
    float cy() const { return (y1 + y2) * 0.5f; }
    float width()  const { return x2 - x1; }
    float height() const { return y2 - y1; }
};

// ─── Frame Pipeline ─────────────────────────────────────────────────────────
class FramePipeline {
public:
    /// Open a video source (camera index as string, or file path / RTSP URL).
    bool open(const std::string& source);

    /// Grab the next frame.  Returns false when stream ends.
    bool read(cv::Mat& frame);

    /// Pre-process a BGR frame into a float blob (NCHW, 0-1 normalized).
    /// @param frame      input BGR image (any size)
    /// @param net_h      network input height
    /// @param net_w      network input width
    /// @param blob       output float vector (1 × 3 × net_h × net_w)
    static void preprocess(const cv::Mat& frame, int net_h, int net_w,
                           std::vector<float>& blob);

    /// Parse raw network output into detections.
    /// YOLOv10 outputs (batch × num_dets × 6) where each row is
    ///   [x1, y1, x2, y2, confidence, class_id].
    /// @param output       raw float output from TRT engine
    /// @param num_dets     number of candidate detections (rows)
    /// @param conf_thresh  minimum confidence to keep
    /// @param orig_w       original frame width  (for rescaling)
    /// @param orig_h       original frame height (for rescaling)
    /// @param net_w        network input width
    /// @param net_h        network input height
    static std::vector<Detection> parse_detections(
        const float* output, int num_dets, float conf_thresh,
        int orig_w, int orig_h, int net_w, int net_h);

    /// Draw detections on frame (in-place).
    static void draw(cv::Mat& frame, const std::vector<Detection>& dets);

    bool is_open() const { return cap_.isOpened(); }

private:
    cv::VideoCapture cap_;
};

}  // namespace golf
