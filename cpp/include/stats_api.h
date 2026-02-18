#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// stats_api.h  –  REST API for Putt Stats
//
// Exposes stats over HTTP so external services (dashboards, mobile apps, etc.)
// can query the current putting session.
//
// Endpoints:
//   GET /api/stats/current  – current putt data
//   GET /api/stats/history  – all completed putts
//   GET /api/stats/session  – session summary (averages)
// ─────────────────────────────────────────────────────────────────────────────

#include "putt_stats.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace golf {

class StatsApi {
public:
    explicit StatsApi(PuttStats& stats, uint16_t port = 8080);
    ~StatsApi();

    StatsApi(const StatsApi&) = delete;
    StatsApi& operator=(const StatsApi&) = delete;

    void start();
    void stop();

private:
    PuttStats& stats_;
    uint16_t port_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void run();
};

}  // namespace golf
