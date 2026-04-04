// ─────────────────────────────────────────────────────────────────────────────
// stats_api.cpp  –  REST API Server for Putt Stats & Tracking
// ─────────────────────────────────────────────────────────────────────────────

#include "stats_api.h"
#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
            << ",\"target_hole_index\":" << b.target_hole_index << "}";
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
        << "\"putter\":{\"x\":" << s.putter.x << ",\"y\":" << s.putter.y
        << ",\"visible\":" << (s.putter.visible ? "true" : "false") << "},"
        << "\"putters\":[";
    for (size_t i = 0; i < s.putters.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"x\":" << s.putters[i].x << ",\"y\":" << s.putters[i].y
            << ",\"visible\":" << (s.putters[i].visible ? "true" : "false") << "}";
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
            << "\"target_hole_index\":" << u.target_hole_index << ","
            << "\"placement_pixel_x\":" << u.placement_pixel_x << ","
            << "\"placement_pixel_y\":" << u.placement_pixel_y << ","
            << "\"placement_hint_valid\":" << (u.placement_hint_valid ? "true" : "false") << ","
            << "\"placement_waiting\":" << (u.placement_waiting ? "true" : "false") << ","
            << "\"placement_after_putt\":" << (u.placement_after_putt ? "true" : "false") << "}";
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
                if (!username.empty()) {
                    // Cross-device: every row with this username gets the same target hole
                    for (auto& u : users_) {
                        if (u.username == username) {
                            u.target_hole_index = idx;
                            ++updated;
                        }
                    }
                } else {
                    for (auto& u : users_) {
                        if (u.session_id == session_id) u.target_hole_index = idx;
                    }
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
            for (auto& u : users_) {
                if (u.ball_index == ball_index) u.ball_index = -1;
            }
            int preserved_target_hole = -1;
            users_.erase(std::remove_if(users_.begin(), users_.end(),
                             [&](const UserState& x) {
                                 if (x.session_id == session_id && x.username == username) {
                                     preserved_target_hole = x.target_hole_index;
                                     return true;
                                 }
                                 return false;
                             }),
                         users_.end());
            UserState nu;
            nu.session_id = session_id;
            nu.username = username;
            nu.ball_index = ball_index;
            nu.target_hole_index = preserved_target_hole;
            users_.push_back(std::move(nu));
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
    for (const auto& u : users_) {
        if (u.ball_index == ball_index && u.target_hole_index >= 0) return u.target_hole_index;
    }
    return -1;
}

std::string StatsApi::get_username_for_ball(int ball_index) const {
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (const auto& u : users_) {
        if (u.ball_index == ball_index) return u.username;
    }
    return "";
}

std::string StatsApi::get_username_for_ball_or_fallback(int ball_index, size_t total_balls) const {
    std::string u = get_username_for_ball(ball_index);
    if (!u.empty()) return u;
    if (total_balls != 1) return "";
    std::lock_guard<std::mutex> lock(users_mutex_);
    const UserState* single = nullptr;
    for (const auto& st : users_) {
        if (st.ball_index >= 0 && !st.username.empty()) {
            if (single) return "";  // multiple claimed users
            single = &st;
        }
    }
    return single ? single->username : "";
}

std::vector<UserState> StatsApi::get_user_states() const {
    std::lock_guard<std::mutex> lock(users_mutex_);
    return users_;
}

void StatsApi::sync_ball_placements_from_tracker(const std::vector<TrackedObject>& balls) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (auto& u : users_) {
        u.ball_track_visible = false;
        if (u.username.empty() || u.ball_index < 0) {
            continue;
        }
        const TrackedObject* tr = nullptr;
        for (const auto& b : balls) {
            if (b.stable_id == u.ball_index) {
                tr = &b;
                break;
            }
        }
        if (!tr) {
            continue;
        }
        if (tr->valid) {
            u.last_known_pixel_x = tr->x;
            u.last_known_pixel_y = tr->y;
            u.last_known_valid = true;
            u.placement_return_after_putt = false;
            u.ball_track_visible = true;
        }
    }
}

void StatsApi::notify_putt_made_for_stable_id(int stable_id) {
    if (stable_id < 0) return;
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (auto& u : users_) {
        if (u.ball_index == stable_id && !u.username.empty()) {
            u.placement_return_after_putt = true;
            return;
        }
    }
}

void StatsApi::try_reassign_placement_return_near_hint(const std::vector<TrackedObject>& balls,
                                                       float max_dist_px) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    const float max_d2 = max_dist_px * max_dist_px;

    auto visible_claim_on = [&](int sid) -> bool {
        for (const auto& u : users_) {
            if (u.ball_index == sid && u.ball_track_visible) return true;
        }
        return false;
    };

    struct Want {
        size_t user_i;
        float px, py;
    };
    std::vector<Want> wants;
    for (size_t i = 0; i < users_.size(); ++i) {
        const auto& u = users_[i];
        if (u.username.empty()) continue;
        if (u.ball_track_visible) continue;
        if (!u.placement_hint_valid) continue;
        // After-putt line hints and occlusion (last-known) hints: reclaimed ball often gets a new stable_id while
        // the reserved ghost stays invalid — without snapping the claim, placement_waiting never clears.
        wants.push_back({i, u.placement_pixel_x, u.placement_pixel_y});
    }
    if (wants.empty()) return;

    struct Cand {
        size_t ball_i;
        int stable_id;
    };
    std::vector<Cand> cands;
    for (size_t bi = 0; bi < balls.size(); ++bi) {
        const auto& b = balls[bi];
        if (!b.valid) continue;
        if (visible_claim_on(b.stable_id)) continue;
        cands.push_back({bi, b.stable_id});
    }
    if (cands.empty()) return;

    struct Pair {
        float d2;
        size_t wi;
        size_t ci;
    };
    std::vector<Pair> pairs;
    for (size_t wi = 0; wi < wants.size(); ++wi) {
        const auto& w = wants[wi];
        for (size_t ci = 0; ci < cands.size(); ++ci) {
            const auto& b = balls[cands[ci].ball_i];
            float dx = b.x - w.px;
            float dy = b.y - w.py;
            float d2 = dx * dx + dy * dy;
            if (d2 <= max_d2) pairs.push_back({d2, wi, ci});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d2 < b.d2; });

    std::vector<bool> used_w(wants.size(), false);
    std::vector<bool> used_c(cands.size(), false);
    for (const auto& p : pairs) {
        if (used_w[p.wi] || used_c[p.ci]) continue;
        used_w[p.wi] = true;
        used_c[p.ci] = true;
        const size_t user_i = wants[p.wi].user_i;
        const int old_bi = users_[user_i].ball_index;
        const int new_sid = cands[p.ci].stable_id;
        if (old_bi == new_sid) continue;
        users_[user_i].ball_index = new_sid;
    }
}

void StatsApi::finalize_placement_hints(float green_center_x, float green_center_y, float line_spacing_px) {
    std::lock_guard<std::mutex> lock(users_mutex_);

    for (auto& u : users_) {
        u.placement_pixel_x = 0.f;
        u.placement_pixel_y = 0.f;
        u.placement_hint_valid = false;
        u.placement_waiting = false;
        u.placement_after_putt = false;
    }

    std::vector<size_t> line_idxs;
    for (size_t i = 0; i < users_.size(); ++i) {
        const UserState& u = users_[i];
        if (u.username.empty() || u.ball_index < 0) continue;
        if (u.placement_return_after_putt && !u.ball_track_visible) {
            line_idxs.push_back(i);
        }
    }
    std::sort(line_idxs.begin(), line_idxs.end(),
              [&](size_t a, size_t b) { return users_[a].username < users_[b].username; });

    const int n_line = static_cast<int>(line_idxs.size());
    if (n_line > 0) {
        const float mid = 0.5f * static_cast<float>(n_line - 1);
        for (int k = 0; k < n_line; ++k) {
            UserState& u = users_[line_idxs[static_cast<size_t>(k)]];
            const float offset = (static_cast<float>(k) - mid) * line_spacing_px;
            u.placement_pixel_x = green_center_x + offset;
            u.placement_pixel_y = green_center_y;
            u.placement_hint_valid = true;
            u.placement_waiting = true;
            u.placement_after_putt = true;
        }
    }

    for (size_t i = 0; i < users_.size(); ++i) {
        UserState& u = users_[i];
        if (u.username.empty() || u.ball_index < 0) continue;
        if (u.ball_track_visible) continue;
        if (u.placement_return_after_putt) continue;
        if (!u.last_known_valid) continue;
        u.placement_pixel_x = u.last_known_pixel_x;
        u.placement_pixel_y = u.last_known_pixel_y;
        u.placement_hint_valid = true;
        u.placement_waiting = true;
        u.placement_after_putt = false;
    }

}

}  // namespace golf
