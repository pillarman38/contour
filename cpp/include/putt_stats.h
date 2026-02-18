#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// putt_stats.h  –  Putting Statistics Tracker
//
// State-machine that monitors the ball tracker and computes per-putt stats:
//   launch speed, peak speed, total distance, break, time in motion, etc.
// ─────────────────────────────────────────────────────────────────────────────

#include "tracker.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace golf {

enum class PuttState { IDLE, IN_MOTION, STOPPED };

struct PuttData {
    int      putt_number    = 0;
    PuttState state         = PuttState::IDLE;

    float launch_speed      = 0.f;   // px/s at first motion
    float current_speed     = 0.f;   // px/s real-time
    float peak_speed        = 0.f;   // px/s max during putt
    float total_distance    = 0.f;   // px cumulative path
    float break_distance    = 0.f;   // px lateral drift from initial line
    float time_in_motion    = 0.f;   // seconds

    float start_x = 0.f, start_y = 0.f;
    float final_x = 0.f, final_y = 0.f;

    const char* state_str() const {
        switch (state) {
            case PuttState::IDLE:      return "idle";
            case PuttState::IN_MOTION: return "in_motion";
            case PuttState::STOPPED:   return "stopped";
        }
        return "unknown";
    }
};

class PuttStats {
public:
    explicit PuttStats(float motion_threshold = 5.f, int stop_frames = 15);

    void update(const TrackedObject& ball, double dt);

    PuttData current() const;
    std::vector<PuttData> history() const;

    struct SessionSummary {
        int   total_putts     = 0;
        float avg_launch_speed = 0.f;
        float avg_distance     = 0.f;
        float avg_break        = 0.f;
        float avg_time         = 0.f;
    };

    SessionSummary session() const;

private:
    float motion_threshold_;
    int   stop_frames_required_;

    mutable std::mutex mu_;
    PuttData current_;
    std::vector<PuttData> history_;

    int   frames_below_threshold_ = 0;
    float prev_x_ = 0.f, prev_y_ = 0.f;
    bool  has_prev_ = false;

    // Initial direction unit vector for break computation
    float dir_x_ = 0.f, dir_y_ = 0.f;
    bool  has_direction_ = false;

    void finalize_putt();
};

}  // namespace golf
