// ─────────────────────────────────────────────────────────────────────────────
// main.cpp  –  Golf Sim: TensorRT Inference Pipeline
//
// Brings together all components:
//   1. Load TensorRT engine
//   2. Capture frames from OpenCV
//   3. Run inference and parse detections
//   4. Track ball & putter
//   5. Compute putt statistics
//   6. Send results to Unreal Engine over UDP
//   7. Expose stats via REST API
// ─────────────────────────────────────────────────────────────────────────────

#include "trt_engine.h"
#include "frame_pipeline.h"
#include "tracker.h"
#include "putt_stats.h"
#include "unreal_sender.h"
#include "stats_api.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

struct Config {
    std::string engine_path;
    std::string video_source = "0";          // camera index or file path
    std::string unreal_host  = "127.0.0.1";
    uint16_t    unreal_port  = 7001;
    uint16_t    api_port     = 8080;
    float       conf_thresh  = 0.5f;
    bool        show_gui     = true;
};

static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [OPTIONS]\n"
        << "\n"
        << "Required:\n"
        << "  --engine PATH        Path to TensorRT .engine file\n"
        << "\n"
        << "Optional:\n"
        << "  --source SRC         Video source: camera id or file path (default: 0)\n"
        << "  --host HOST          Unreal Engine UDP host (default: 127.0.0.1)\n"
        << "  --port PORT          Unreal Engine UDP port (default: 7001)\n"
        << "  --api-port PORT      REST API port for stats (default: 8080)\n"
        << "  --conf THRESH        Detection confidence threshold (default: 0.5)\n"
        << "  --no-gui             Disable OpenCV preview window\n"
        << "  -h, --help           Show this help\n";
}

static Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--engine") && i + 1 < argc) {
            cfg.engine_path = argv[++i];
        } else if ((arg == "--source") && i + 1 < argc) {
            cfg.video_source = argv[++i];
        } else if ((arg == "--host") && i + 1 < argc) {
            cfg.unreal_host = argv[++i];
        } else if ((arg == "--port") && i + 1 < argc) {
            cfg.unreal_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "--api-port") && i + 1 < argc) {
            cfg.api_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "--conf") && i + 1 < argc) {
            cfg.conf_thresh = std::stof(argv[++i]);
        } else if (arg == "--no-gui") {
            cfg.show_gui = false;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }
    if (cfg.engine_path.empty()) {
        std::cerr << "Error: --engine is required\n\n";
        print_usage(argv[0]);
        std::exit(1);
    }
    return cfg;
}

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);

    // ── 1. Load TensorRT Engine ─────────────────────────────────────────
    golf::TrtEngine engine;
    if (!engine.load(cfg.engine_path)) {
        return 1;
    }

    // ── 2. Open Video Source ────────────────────────────────────────────
    golf::FramePipeline pipeline;
    if (!pipeline.open(cfg.video_source)) {
        return 1;
    }

    // ── 3. Init UDP Sender ──────────────────────────────────────────────
    golf::UnrealSender sender;
    if (!sender.init(cfg.unreal_host, cfg.unreal_port)) {
        std::cerr << "[WARN] UDP sender init failed – running without UE link\n";
    }

    // ── 4. Init Tracker ─────────────────────────────────────────────────
    golf::Tracker tracker(/*alpha=*/0.6f, /*max_lost=*/15);

    // ── 5. Init Putt Stats ──────────────────────────────────────────────
    golf::PuttStats putt_stats(/*motion_threshold=*/5.f, /*stop_frames=*/15);

    // ── 6. Start REST API ───────────────────────────────────────────────
    golf::StatsApi api(putt_stats, cfg.api_port);
    api.start();

    // ── 7. Main Loop ────────────────────────────────────────────────────
    cv::Mat frame;
    std::vector<float> blob;
    std::vector<float> output;

    auto prev_time = std::chrono::steady_clock::now();
    int frame_count = 0;

    std::cout << "[Main] Entering inference loop (press 'q' to quit)\n";

    while (pipeline.read(frame)) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - prev_time).count();
        prev_time = now;

        int orig_w = frame.cols;
        int orig_h = frame.rows;

        // Pre-process
        golf::FramePipeline::preprocess(
            frame, engine.input_h(), engine.input_w(), blob);

        // Infer
        if (!engine.infer(blob.data(), output)) {
            std::cerr << "[Main] Inference failed on frame " << frame_count << "\n";
            continue;
        }

        // Parse detections
        int num_dets = static_cast<int>(output.size()) / 6;
        auto detections = golf::FramePipeline::parse_detections(
            output.data(), num_dets, cfg.conf_thresh,
            orig_w, orig_h, engine.input_w(), engine.input_h());

        // Track
        tracker.update(detections, dt);

        // Compute putt stats
        putt_stats.update(tracker.ball(), dt);

        // Send to Unreal Engine
        sender.send(tracker.ball(), tracker.putter(), putt_stats.current());

        // Visualise
        if (cfg.show_gui) {
            golf::FramePipeline::draw(frame, detections);

            // Overlay tracker info
            char info[128];
            if (tracker.ball_visible()) {
                std::snprintf(info, sizeof(info),
                    "Ball: (%.0f, %.0f) v=(%.0f, %.0f) px/s",
                    tracker.ball().x, tracker.ball().y,
                    tracker.ball().vx, tracker.ball().vy);
                cv::putText(frame, info, cv::Point(10, 25),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 255, 0), 2);
            }
            if (tracker.putter_visible()) {
                std::snprintf(info, sizeof(info),
                    "Putter: (%.0f, %.0f)",
                    tracker.putter().x, tracker.putter().y);
                cv::putText(frame, info, cv::Point(10, 50),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(255, 0, 255), 2);
            }

            // Putt stats overlay
            auto stats = putt_stats.current();
            std::snprintf(info, sizeof(info), "Putt #%d [%s]",
                stats.putt_number, stats.state_str());
            cv::putText(frame, info, cv::Point(10, 80),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 255), 1);

            std::snprintf(info, sizeof(info),
                "Speed: %.1f  Peak: %.1f  Dist: %.1f  Break: %.1f",
                stats.current_speed, stats.peak_speed,
                stats.total_distance, stats.break_distance);
            cv::putText(frame, info, cv::Point(10, 100),
                        cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(0, 255, 255), 1);

            // FPS
            double fps = (dt > 1e-6) ? 1.0 / dt : 0.0;
            std::snprintf(info, sizeof(info), "FPS: %.1f", fps);
            cv::putText(frame, info, cv::Point(10, orig_h - 15),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(255, 255, 255), 2);

            cv::imshow("Golf Sim – Detection", frame);
            if (cv::waitKey(1) == 'q') break;
        }

        frame_count++;
    }

    std::cout << "[Main] Processed " << frame_count << " frames\n";
    api.stop();
    sender.close();
    return 0;
}
