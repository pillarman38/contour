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
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
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

    golf::PuttStats putt_stats(2.0f, 45);  // 45 frames (~1.5s at 30fps) low speed before STOPPED; reduces spurious transitions during slow rolls
    auto prev_time = std::chrono::steady_clock::now();

    golf::TrackingState tracking_state;
    golf::StatsApi api(putt_stats, &tracking_state, cfg.api_port);
    api.start();

    // --- 3. Main Loop ---
    cv::Mat frame;
    std::vector<float> blob;
    std::vector<float> output;
    int frame_count = 0;
    int putt_made_frames = 0;
    const int putt_made_hold = 60;
    // Snapshot of the last completed putt's stats (used for "putt made" payloads)
    golf::PuttData last_completed_stats;
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
        int target_idx = api.get_target_hole_index();
        tracker.set_target_hole_index(target_idx);
        bool ball_was_visible = tracker.ball_visible();
        {
            std::unordered_set<int> reserved_stable;
            for (const auto& u : api.get_user_states()) {
                if (!u.username.empty() && u.ball_index >= 0) reserved_stable.insert(u.ball_index);
            }
            tracker.set_reserved_stable_ids(std::move(reserved_stable));
        }
        tracker.update(detections, dt);

        if (ball_was_visible && !tracker.ball_visible()) {
            putt_stats.on_ball_lost();  // Ball lost before slowing down: transition to STOPPED so UE can fade
        }
        // In your main loop:
        float current_ppi = 1.0f;
        if (tracker.hole_pos().valid) {
            // Hole is 4.25 inches wide. r * 2 = diameter.
            current_ppi = (tracker.hole_pos().radius * 2.0f) / 4.25f;
        }

        // Compute putt stats (pass PPI so values are in inches, not pixels)
        putt_stats.update(tracker.ball(), dt, current_ppi);

        // New putt started (ball in motion) — allow "Putt ended" to fire again next time ball is lost
        if (putt_stats.current().state == golf::PuttState::IN_MOTION) {
            tracker.reset_for_new_putt();
        }

        // Hold putt_made true for multiple frames so UE reliably catches it.
        // When the putt is first detected as made, snapshot the current stats so the
        // payload that carries putt_made=true also carries this putt's numbers.
        if (tracker.is_putt_made && putt_made_frames == 0) {
            api.notify_putt_made_for_stable_id(tracker.ball().stable_id);
            putt_made_frames = putt_made_hold;
            last_completed_stats = putt_stats.current();
            last_completed_stats.state = golf::PuttState::STOPPED;
            tracker.reset_putt();
        }
        bool send_putt_made = putt_made_frames > 0;
        if (putt_made_frames > 0) putt_made_frames--;

        // Only send completed putt stats when putt is STOPPED; otherwise send zeros so UI shows 0 until next putt completes.
        auto current = putt_stats.current();
        golf::PuttData stats_to_send;
        stats_to_send.state = current.state;  // Always send current state (idle/in_motion/stopped) for line freeze etc.

        // When PuttStats reaches STOPPED, refresh our snapshot of the completed putt.
        if (current.state == golf::PuttState::STOPPED) {
            last_completed_stats = current;
        }

        // For frames where we flag putt_made=true, send the completed-putt snapshot
        // so Unreal's OnPuttMade event sees non-zero stats for that putt.
        // Send full stats for both IN_MOTION and STOPPED so Unreal can show markers (needs StartPos, LaunchSpeed for bHasLaunchPos).
        if (send_putt_made && last_completed_stats.putt_number > 0) {
            stats_to_send = last_completed_stats;
        } else if (current.state == golf::PuttState::STOPPED || current.state == golf::PuttState::IN_MOTION) {
            stats_to_send = current;  // Full stats so Unreal displays launch/peak/stop markers.
        }

        // Placement hints: after made putt → green-centre line; occlusion only → last known (sync tracks visibility).
        float green_cx = static_cast<float>(orig_w) * 0.5f;
        float green_cy = static_cast<float>(orig_h) * 0.5f;
        {
            float hsx = 0.f, hsy = 0.f;
            int hn = 0;
            for (const auto& h : tracker.holes()) {
                if (h.valid) {
                    hsx += h.x;
                    hsy += h.y;
                    ++hn;
                }
            }
            if (hn > 0) {
                green_cx = hsx / static_cast<float>(hn);
                green_cy = hsy / static_cast<float>(hn);
            }
        }
        api.sync_ball_placements_from_tracker(tracker.balls());
        api.finalize_placement_hints(green_cx, green_cy, 90.f);
        // Made-putt ghost is often far from green-center hint; new detection gets a new stable_id — snap claim to it.
        api.try_reassign_placement_return_near_hint(tracker.balls(), 320.f);
        api.sync_ball_placements_from_tracker(tracker.balls());
        api.finalize_placement_hints(green_cx, green_cy, 90.f);

        // Update tracking snapshot for GET /api/tracking (after placement finalization)
        {
            std::lock_guard<std::mutex> lock(tracking_state.mutex);
            int api_ti = api.get_target_hole_index();
            tracking_state.snapshot.target_hole_index = (api_ti >= 0) ? api_ti : tracker.primary_hole_index();
            tracking_state.snapshot.target_hole_x = tracker.hole_pos().x;
            tracking_state.snapshot.target_hole_y = tracker.hole_pos().y;
            tracking_state.snapshot.frame_width = orig_w;
            tracking_state.snapshot.frame_height = orig_h;
            tracking_state.snapshot.balls.clear();
            for (const auto& b : tracker.balls()) {
                golf::TrackingSnapshot::BallInfo bi;
                bi.x = b.x;
                bi.y = b.y;
                bi.visible = b.valid;
                bi.index = b.stable_id;
                bi.username = api.get_username_for_ball_or_fallback(b.stable_id, tracker.balls().size());
                bi.target_hole_index = api.get_target_hole_for_ball(b.stable_id);
                tracking_state.snapshot.balls.push_back(bi);
            }
            tracking_state.snapshot.putter.x = tracker.putter().x;
            tracking_state.snapshot.putter.y = tracker.putter().y;
            tracking_state.snapshot.putter.visible = tracker.putter().valid;
            tracking_state.snapshot.putters.clear();
            for (const auto& p : tracker.putters()) {
                golf::TrackingSnapshot::PutterInfo pi;
                pi.x = p.x;
                pi.y = p.y;
                pi.visible = p.valid;
                tracking_state.snapshot.putters.push_back(pi);
            }
            tracking_state.snapshot.users = api.get_user_states();
            tracking_state.snapshot.holes.clear();
            for (const auto& h : tracker.holes()) {
                golf::TrackingSnapshot::HoleInfo hi;
                hi.x = h.x;
                hi.y = h.y;
                hi.radius = h.radius;
                hi.visible = h.valid;
                tracking_state.snapshot.holes.push_back(hi);
            }
        }

        std::vector<golf::UnrealSender::BallPayload> ball_payloads;
        const auto& balls_vec = tracker.balls();
        int ti;
        int ti_raw = -999;
        if (balls_vec.empty()) {
            ti = api.get_target_hole_index();
        } else {
            ti_raw = api.get_target_hole_for_ball(balls_vec[0].stable_id);
            ti = ti_raw;
        }
        if (ti < 0) ti = tracker.primary_hole_index();
        // Hole position for aim line: user's selected hole or tracker primary
        const auto& holes_vec = tracker.holes();
        float thx, thy;
        if (ti >= 0 && static_cast<size_t>(ti) < holes_vec.size()) {
            thx = holes_vec[ti].x;
            thy = holes_vec[ti].y;
        } else {
            const auto& hp = tracker.hole_pos();
            thx = hp.x;
            thy = hp.y;
        }
        auto fill_ball_aim = [&](golf::UnrealSender::BallPayload& bp, const golf::TrackedObject& ball_obj) {
            int tib = api.get_target_hole_for_ball(ball_obj.stable_id);
            if (tib < 0) tib = tracker.primary_hole_index();
            bp.target_hole_index = tib;
            if (tib >= 0 && static_cast<size_t>(tib) < holes_vec.size()) {
                bp.target_hole_x = holes_vec[static_cast<size_t>(tib)].x;
                bp.target_hole_y = holes_vec[static_cast<size_t>(tib)].y;
            } else {
                const auto& hp = tracker.hole_pos();
                bp.target_hole_x = hp.x;
                bp.target_hole_y = hp.y;
            }
        };

        if (balls_vec.empty()) {
            golf::UnrealSender::BallPayload bp;
            bp.ball = &tracker.ball();
            bp.username = tracker.ball().valid ? api.get_username_for_ball_or_fallback(tracker.ball().stable_id, 1) : "";
            bp.stats = stats_to_send;
            bp.is_putt_made = send_putt_made;
            if (tracker.ball().valid) {
                fill_ball_aim(bp, tracker.ball());
            }
            ball_payloads.push_back(bp);
        } else {
            for (size_t i = 0; i < balls_vec.size(); ++i) {
                golf::UnrealSender::BallPayload bp;
                bp.ball = &balls_vec[i];
                bp.username = api.get_username_for_ball_or_fallback(balls_vec[i].stable_id, balls_vec.size());
                bp.stats = (i == 0) ? stats_to_send : golf::PuttData{};
                bp.is_putt_made = (i == 0) ? send_putt_made : false;
                fill_ball_aim(bp, balls_vec[i]);
                ball_payloads.push_back(bp);
            }
        }
        std::vector<golf::BallPlacementHint> placement_hints;
        for (const auto& u : api.get_user_states()) {
            if (u.username.empty() || u.ball_index < 0 || !u.placement_hint_valid) continue;
            placement_hints.push_back({u.username, u.ball_index, u.placement_pixel_x, u.placement_pixel_y,
                                     u.placement_waiting, u.placement_after_putt});
        }
        sender.send(ball_payloads, tracker.putter(), tracker.holes(), ti, thx, thy, placement_hints);

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

            {
                float ball_speed_px = std::sqrt(tracker.ball().vx * tracker.ball().vx +
                                                tracker.ball().vy * tracker.ball().vy);
                std::snprintf(info, sizeof(info),
                    "distance from hole: %f, ball speed: %f",
                    tracker.min_dist_px, ball_speed_px);
                cv::putText(frame, info, cv::Point(10, 80),
                            cv::FONT_HERSHEY_SIMPLEX, 0.9,
                            cv::Scalar(0, 200, 255), 1);
            }

            // Putt made banner
            if (send_putt_made) {
                cv::putText(frame, "PUTT MADE!", cv::Point(orig_w / 2 - 150, orig_h / 2),
                            cv::FONT_HERSHEY_SIMPLEX, 1.5,
                            cv::Scalar(0, 255, 0), 3);
            }

            // Putt stats overlay
            auto stats = putt_stats.current();
            std::snprintf(info, sizeof(info), "Putt #%d [%s]",
                stats.putt_number, stats.state_str());
            cv::putText(frame, info, cv::Point(10, 110),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 255), 1);

            std::snprintf(info, sizeof(info),
                "Speed: %.1f  Peak: %.1f  Dist: %.1f  Break: %.1f",
                stats.current_speed, stats.peak_speed,
                stats.total_distance, stats.break_distance);
            cv::putText(frame, info, cv::Point(10, 130),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 255), 1);

            // Confidence & tracking health
            std::snprintf(info, sizeof(info),
                "Conf  Ball: %.0f%%  Putter: %.0f%%  Lost: %d / %d",
                tracker.ball().confidence * 100.f,
                tracker.putter().confidence * 100.f,
                tracker.ball().frames_since_seen,
                tracker.putter().frames_since_seen);
            cv::putText(frame, info, cv::Point(10, 155),
                        cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(180, 180, 180), 1);

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
