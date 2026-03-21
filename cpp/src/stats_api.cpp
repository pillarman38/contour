// ─────────────────────────────────────────────────────────────────────────────
// stats_api.cpp  –  REST API Server for Putt Stats & Tracking
// ─────────────────────────────────────────────────────────────────────────────

#include "stats_api.h"
#include "httplib.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

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

static std::string escape_json_str(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        if (c == '"') oss << "\\\"";
        else if (c == '\\') oss << "\\\\";
        else if (c == '\n') oss << "\\n";
        else oss << c;
    }
    return oss.str();
}

static std::string tracking_snapshot_json(const TrackingSnapshot& s) {
    std::ostringstream oss;
    oss << "{";
    // Legacy "ball" = first ball for backward compat
    if (!s.balls.empty()) {
        const auto& b = s.balls[0];
        oss << "\"ball\":{\"x\":" << b.x << ",\"y\":" << b.y
            << ",\"visible\":" << (b.visible ? "true" : "false") << "},";
    } else {
        oss << "\"ball\":{\"x\":0,\"y\":0,\"visible\":false},";
    }
    oss << "\"balls\":[";
    for (size_t i = 0; i < s.balls.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& b = s.balls[i];
        oss << "{\"x\":" << b.x << ",\"y\":" << b.y
            << ",\"visible\":" << (b.visible ? "true" : "false")
            << ",\"index\":" << b.index
            << ",\"username\":\"" << escape_json_str(b.username) << "\""
            << ",\"target_hole_index\":" << (b.target_hole_index >= 0 ? b.target_hole_index : 0) << "}";
    }
    oss << "],"
        << "\"target_hole_index\":" << s.target_hole_index << ","
        << "\"target_hole_x\":" << s.target_hole_x << ",\"target_hole_y\":" << s.target_hole_y << ","
        << "\"holes\":[";
    for (size_t i = 0; i < s.holes.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& h = s.holes[i];
        oss << "{\"x\":" << h.x << ",\"y\":" << h.y
            << ",\"radius\":" << h.radius
            << ",\"visible\":" << (h.visible ? "true" : "false") << "}";
    }
    oss << "],"
        << "\"frame_width\":" << s.frame_width << ","
        << "\"frame_height\":" << s.frame_height;
    // Users array for cross-device sync (username, ball_index, target_hole_index)
    oss << ",\"users\":[";
    for (size_t i = 0; i < s.users.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& u = s.users[i];
        oss << "{\"username\":\"" << escape_json_str(u.username) << "\","
            << "\"ball_index\":" << u.ball_index << ","
            << "\"target_hole_index\":" << (u.target_hole_index >= 0 ? u.target_hole_index : 0) << "}";
    }
    oss << "]}";
    return oss.str();
}

StatsApi::StatsApi(PuttStats& stats, uint16_t port)
    : stats_(stats), port_(port) {}

StatsApi::StatsApi(PuttStats& stats, TrackingState* tracking, uint16_t port)
    : stats_(stats), tracking_(tracking), port_(port) {}

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
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
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

    if (tracking_) {
        svr.Get("/api/tracking", [this](const httplib::Request&, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(tracking_->mutex);
            res.set_content(tracking_snapshot_json(tracking_->snapshot), "application/json");
        });

        svr.Post("/api/target-hole", [this](const httplib::Request& req, httplib::Response& res) {
            auto find_int = [&req](const char* key) {
                std::string k(key);
                size_t p = req.body.find("\"" + k + "\"");
                if (p == std::string::npos) return -1;
                p = req.body.find(':', p);
                if (p == std::string::npos) return -1;
                return static_cast<int>(std::strtol(req.body.c_str() + p + 1, nullptr, 10));
            };
            auto find_str = [&req](const char* key) {
                std::string k(key);
                size_t p = req.body.find("\"" + k + "\"");
                if (p == std::string::npos) return std::string();
                p = req.body.find(':', p);
                if (p == std::string::npos) return std::string();
                p = req.body.find('"', p);
                if (p == std::string::npos) return std::string();
                size_t start = p + 1;
                size_t end = req.body.find('"', start);
                if (end == std::string::npos) return std::string();
                return req.body.substr(start, end - start);
            };
            int idx = find_int("index");
            std::string session_id = find_str("session_id");
            std::string username = find_str("username");
            int updated = 0;
            if (session_id.empty()) {
                target_hole_index_.store(idx);
            } else {
                std::lock_guard<std::mutex> lock(users_mutex_);
                UserState& u = users_[session_id];
                u.session_id = session_id;
                u.target_hole_index = idx;
                if (!username.empty()) {
                    u.username = username;
                    // Update ALL users with same username (cross-device sync: any machine can update)
                    for (auto& kv : users_) {
                        if (kv.second.username == username) {
                            kv.second.target_hole_index = idx;
                            updated++;
                        }
                    }
                } else {
                    u.target_hole_index = idx;
                }
            }
            res.set_content("{\"ok\":true,\"index\":" + std::to_string(idx) + "}", "application/json");
        });

        svr.Post("/api/claim-ball", [this](const httplib::Request& req, httplib::Response& res) {
            std::string session_id, username;
            int ball_index = -1;
            // Simple JSON parse for session_id, ball_index, username
            auto find_int = [&req](const char* key) {
                std::string k(key);
                size_t p = req.body.find("\"" + k + "\"");
                if (p == std::string::npos) return -1;
                p = req.body.find(':', p);
                if (p == std::string::npos) return -1;
                return static_cast<int>(std::strtol(req.body.c_str() + p + 1, nullptr, 10));
            };
            auto find_str = [&req](const char* key) {
                std::string k(key);
                size_t p = req.body.find("\"" + k + "\"");
                if (p == std::string::npos) return std::string();
                p = req.body.find(':', p);
                if (p == std::string::npos) return std::string();
                p = req.body.find('"', p);
                if (p == std::string::npos) return std::string();
                size_t start = p + 1;
                size_t end = req.body.find('"', start);
                if (end == std::string::npos) return std::string();
                return req.body.substr(start, end - start);
            };
            session_id = find_str("session_id");
            username = find_str("username");
            ball_index = find_int("ball_index");
            if (session_id.empty() || username.empty() || ball_index < 0) {
                res.status = 400;
                res.set_content("{\"ok\":false,\"error\":\"session_id, username, ball_index required\"}", "application/json");
                return;
            }
            std::lock_guard<std::mutex> lock(users_mutex_);
            for (auto& kv : users_) {
                if (kv.second.ball_index == ball_index) kv.second.ball_index = -1;
            }
            UserState& u = users_[session_id];
            u.session_id = session_id;
            u.username = username;
            u.ball_index = ball_index;
            res.set_content("{\"ok\":true,\"ball_index\":" + std::to_string(ball_index) + ",\"username\":\"" + escape_json_str(username) + "\"}", "application/json");
        });

        svr.Get("/api/session-id", [this](const httplib::Request&, httplib::Response& res) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> hex(0, 15);
            char id[33] = {0};
            for (int i = 0; i < 32; i++) id[i] = "0123456789abcdef"[hex(gen)];
            std::string session_id(id);
            res.set_content("{\"session_id\":\"" + session_id + "\"}", "application/json");
        });
    }

    svr.Options("/(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("", "text/plain");
    });

    while (running_) {
        svr.listen("0.0.0.0", port_);
    }
}

int StatsApi::get_target_hole_for_ball(int ball_index) const {
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (const auto& kv : users_) {
        if (kv.second.ball_index == ball_index && kv.second.target_hole_index >= 0)
            return kv.second.target_hole_index;
    }
    return -1;
}

std::string StatsApi::get_username_for_ball(int ball_index) const {
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (const auto& kv : users_) {
        if (kv.second.ball_index == ball_index)
            return kv.second.username;
    }
    return "";
}

std::string StatsApi::get_username_for_ball_or_fallback(int ball_index, size_t total_balls) const {
    std::string u = get_username_for_ball(ball_index);
    if (!u.empty()) return u;
    if (total_balls != 1) return "";
    std::lock_guard<std::mutex> lock(users_mutex_);
    const UserState* single = nullptr;
    for (const auto& kv : users_) {
        if (kv.second.ball_index >= 0 && !kv.second.username.empty()) {
            if (single) return "";  // multiple claimed users
            single = &kv.second;
        }
    }
    return single ? single->username : "";
}

std::vector<UserState> StatsApi::get_user_states() const {
    std::lock_guard<std::mutex> lock(users_mutex_);
    std::vector<UserState> out;
    for (const auto& kv : users_) {
        out.push_back(kv.second);
    }
    return out;
}

}  // namespace golf
