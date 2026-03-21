#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// stats_api.h  –  REST API for Putt Stats & Tracking
//
// Exposes stats over HTTP so external services (dashboards, mobile apps, etc.)
// can query the current putting session.
//
// Endpoints:
//   GET /api/stats/current   – current putt data
//   GET /api/stats/history   – all completed putts
//   GET /api/stats/session   – session summary (averages)
//   GET /api/tracking       – ball, holes, frame dimensions (for top-down view)
//   POST /api/target-hole   – set target hole index { "index": 0 }
// ─────────────────────────────────────────────────────────────────────────────

#include "putt_stats.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace golf {

/// Per-user state: target hole + claimed ball index + username.
struct UserState {
    std::string session_id;
    std::string username;
    int ball_index = -1;      // -1 = not claimed
    int target_hole_index = -1;
};

/// Snapshot of tracking data for GET /api/tracking.
struct TrackingSnapshot {
    int frame_width = 1920, frame_height = 1080;
    int target_hole_index = 0;  // legacy single-user
    float target_hole_x = 0.f, target_hole_y = 0.f;

    struct BallInfo {
        float x = 0.f, y = 0.f;
        bool visible = false;
        int index = 0;
        std::string username;
        int target_hole_index = -1;  // owner's target hole (for multi-device sync)
    };
    std::vector<BallInfo> balls;

    struct HoleInfo {
        float x = 0.f, y = 0.f, radius = 0.f;
        bool visible = false;
    };
    std::vector<HoleInfo> holes;

    std::vector<UserState> users;
};

/// Shared state for tracking snapshot (main writes, StatsApi reads).
struct TrackingState {
    std::mutex mutex;
    TrackingSnapshot snapshot;
};

class StatsApi {
public:
    explicit StatsApi(PuttStats& stats, uint16_t port = 8080);
    StatsApi(PuttStats& stats, TrackingState* tracking, uint16_t port = 8080);
    ~StatsApi();

    StatsApi(const StatsApi&) = delete;
    StatsApi& operator=(const StatsApi&) = delete;

    void start();
    void stop();

    /// Target hole index set by POST /api/target-hole (-1 = use auto-selection). Legacy single-user.
    int get_target_hole_index() const { return target_hole_index_.load(); }

    /// Target hole index for a specific ball (from user who claimed it). -1 if none.
    int get_target_hole_for_ball(int ball_index) const;

    /// Username for ball index. Empty if not claimed.
    std::string get_username_for_ball(int ball_index) const;

    /// Username for ball, with fallback when exactly one ball and one claimed user (handles stable_id recycling).
    std::string get_username_for_ball_or_fallback(int ball_index, size_t total_balls) const;

    /// All user states (for main loop to build per-ball stats).
    std::vector<UserState> get_user_states() const;

private:
    PuttStats& stats_;
    TrackingState* tracking_ = nullptr;
    uint16_t port_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> target_hole_index_{-1};

    mutable std::mutex users_mutex_;
    std::map<std::string, UserState> users_;  // session_id -> state

    void run();
};

}  // namespace golf
