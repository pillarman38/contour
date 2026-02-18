// ─────────────────────────────────────────────────────────────────────────────
// stats_api.cpp  –  REST API Server for Putt Stats
// ─────────────────────────────────────────────────────────────────────────────

#include "stats_api.h"
#include "httplib.h"

#include <cstdio>
#include <iostream>
#include <sstream>

namespace golf {

static std::string putt_data_json(const PuttData& p) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{"
            "\"putt_number\":%d,"
            "\"state\":\"%s\","
            "\"launch_speed\":%.2f,"
            "\"current_speed\":%.2f,"
            "\"peak_speed\":%.2f,"
            "\"total_distance\":%.2f,"
            "\"break_distance\":%.2f,"
            "\"time_in_motion\":%.2f,"
            "\"start_x\":%.2f,\"start_y\":%.2f,"
            "\"final_x\":%.2f,\"final_y\":%.2f"
        "}",
        p.putt_number, p.state_str(),
        p.launch_speed, p.current_speed,
        p.peak_speed, p.total_distance,
        p.break_distance, p.time_in_motion,
        p.start_x, p.start_y,
        p.final_x, p.final_y);
    return buf;
}

StatsApi::StatsApi(PuttStats& stats, uint16_t port)
    : stats_(stats), port_(port) {}

StatsApi::~StatsApi() {
    stop();
}

void StatsApi::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&StatsApi::run, this);
    std::cout << "[StatsApi] HTTP server starting on port " << port_ << "\n";
}

void StatsApi::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void StatsApi::run() {
    httplib::Server svr;

    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    svr.Get("/api/stats/current", [this](const httplib::Request&, httplib::Response& res) {
        auto data = stats_.current();
        res.set_content(putt_data_json(data), "application/json");
    });

    svr.Get("/api/stats/history", [this](const httplib::Request&, httplib::Response& res) {
        auto hist = stats_.history();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < hist.size(); ++i) {
            if (i > 0) oss << ",";
            oss << putt_data_json(hist[i]);
        }
        oss << "]";
        res.set_content(oss.str(), "application/json");
    });

    svr.Get("/api/stats/session", [this](const httplib::Request&, httplib::Response& res) {
        auto s = stats_.session();
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{"
                "\"total_putts\":%d,"
                "\"avg_launch_speed\":%.2f,"
                "\"avg_distance\":%.2f,"
                "\"avg_break\":%.2f,"
                "\"avg_time\":%.2f"
            "}",
            s.total_putts, s.avg_launch_speed,
            s.avg_distance, s.avg_break, s.avg_time);
        res.set_content(buf, "application/json");
    });

    svr.Options("/(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("", "text/plain");
    });

    while (running_) {
        svr.listen("0.0.0.0", port_);
    }
}

}  // namespace golf
