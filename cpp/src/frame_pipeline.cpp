// ─────────────────────────────────────────────────────────────────────────────
// frame_pipeline.cpp  –  OpenCV Frame Capture, Pre-processing & Parsing
// ─────────────────────────────────────────────────────────────────────────────

#include "frame_pipeline.h"

#include <algorithm>
#include <iostream>

namespace golf {

static const cv::Scalar kColors[] = {
    {0, 255, 0},    // golf_ball  → green
    {255, 0, 255},  // putter     → magenta
};
static const char* kClassNames[] = {"golf_ball", "putter"};

// ─── Open ───────────────────────────────────────────────────────────────────
bool FramePipeline::open(const std::string& source) {
    // Try to interpret as camera index
    bool all_digits = !source.empty() &&
        std::all_of(source.begin(), source.end(), ::isdigit);

    if (all_digits) {
        int cam_id = std::stoi(source);
        cap_.open(cam_id);
    } else {
        cap_.open(source);
    }

    if (!cap_.isOpened()) {
        std::cerr << "[FramePipeline] Cannot open source: " << source << "\n";
        return false;
    }

    std::cout << "[FramePipeline] Opened: " << source
              << " (" << static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH))
              << "x" << static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT))
              << " @ " << cap_.get(cv::CAP_PROP_FPS) << " fps)\n";
    return true;
}

// ─── Read ───────────────────────────────────────────────────────────────────
bool FramePipeline::read(cv::Mat& frame) {
    return cap_.read(frame) && !frame.empty();
}

// ─── Preprocess ─────────────────────────────────────────────────────────────
void FramePipeline::preprocess(const cv::Mat& frame, int net_h, int net_w,
                               std::vector<float>& blob) {
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(net_w, net_h));

    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

    // HWC → CHW
    const int area = net_h * net_w;
    blob.resize(3 * area);
    const float* ptr = reinterpret_cast<const float*>(rgb.data);
    for (int i = 0; i < area; ++i) {
        blob[0 * area + i] = ptr[i * 3 + 0];  // R
        blob[1 * area + i] = ptr[i * 3 + 1];  // G
        blob[2 * area + i] = ptr[i * 3 + 2];  // B
    }
}

// ─── Parse Detections ───────────────────────────────────────────────────────
std::vector<Detection> FramePipeline::parse_detections(
    const float* output, int num_dets, float conf_thresh,
    int orig_w, int orig_h, int net_w, int net_h)
{
    std::vector<Detection> dets;

    const float sx = static_cast<float>(orig_w) / net_w;
    const float sy = static_cast<float>(orig_h) / net_h;

    // YOLOv10 output: each row is [x1, y1, x2, y2, conf, class_id]
    for (int i = 0; i < num_dets; ++i) {
        const float* row = output + i * 6;

        float conf = row[4];
        if (conf < conf_thresh) continue;

        Detection d;
        d.x1 = row[0] * sx;
        d.y1 = row[1] * sy;
        d.x2 = row[2] * sx;
        d.y2 = row[3] * sy;
        d.confidence = conf;
        d.class_id = static_cast<int>(row[5]);

        dets.push_back(d);
    }
    return dets;
}

// ─── Draw ───────────────────────────────────────────────────────────────────
void FramePipeline::draw(cv::Mat& frame, const std::vector<Detection>& dets) {
    for (const auto& d : dets) {
        int cid = std::clamp(d.class_id, 0, 1);
        cv::Scalar color = kColors[cid];

        cv::rectangle(frame,
                      cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1)),
                      cv::Point(static_cast<int>(d.x2), static_cast<int>(d.y2)),
                      color, 2);

        char label[64];
        std::snprintf(label, sizeof(label), "%s %.0f%%",
                      kClassNames[cid], d.confidence * 100.f);

        int baseline = 0;
        cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                       0.5, 1, &baseline);

        cv::rectangle(frame,
                      cv::Point(static_cast<int>(d.x1),
                                static_cast<int>(d.y1) - ts.height - 6),
                      cv::Point(static_cast<int>(d.x1) + ts.width + 4,
                                static_cast<int>(d.y1)),
                      color, cv::FILLED);

        cv::putText(frame, label,
                    cv::Point(static_cast<int>(d.x1) + 2,
                              static_cast<int>(d.y1) - 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255), 1);

        // Center dot
        cv::circle(frame,
                   cv::Point(static_cast<int>(d.cx()),
                             static_cast<int>(d.cy())),
                   3, color, cv::FILLED);
    }
}

}  // namespace golf
