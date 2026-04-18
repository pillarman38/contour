// ─────────────────────────────────────────────────────────────────────────────
// stats_api.cpp  –  REST API Server for Putt Stats & Tracking
// ─────────────────────────────────────────────────────────────────────────────

#include "stats_api.h"
#include "httplib.h"

#include <algorithm>
#include <cmath>
#include <chrono>
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
    if (s.hole_aim_ball_index_set) {
        oss << ",\"hole_aim_ball_index\":" << s.hole_aim_ball_index;
    }
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
            int ball_index = find_int("ball_index");
            if (ball_index >= 0) {
                // Set target hole directly by ball stable index (works for any ball, claimed or not)
                std::lock_guard<std::mutex> lock(users_mutex_);
                bool found = false;
                for (auto& u : users_) {
                    if (u.ball_index == ball_index) {
                        u.target_hole_index = idx;
                        u.target_hole_anchor_valid = false;
                        found = true;
                    }
                }
                if (!found) {
                    UserState nu;
                    nu.session_id = session_id;
                    nu.ball_index = ball_index;
                    nu.target_hole_index = idx;
                    nu.target_hole_anchor_valid = false;
                    users_.push_back(std::move(nu));
                }
            } else if (session_id.empty()) {
                target_hole_index_.store(idx);
            } else {
                std::lock_guard<std::mutex> lock(users_mutex_);
                if (!username.empty()) {
                    for (auto& u : users_) {
                        if (u.username == username) {
                            u.target_hole_index = idx;
                            u.target_hole_anchor_valid = false;
                        }
                    }
                } else {
                    for (auto& u : users_) {
                        if (u.session_id == session_id) {
                            u.target_hole_index = idx;
                            u.target_hole_anchor_valid = false;
                        }
                    }
                }
            }
            res.set_content("{\"ok\":true,\"index\":" + std::to_string(idx) + "}", "application/json");
        });

        svr.Post("/api/hole-aim-selection", [this](const httplib::Request& req, httplib::Response& res) {
            auto find_int = [&req](const char* key) {
                std::string k(key);
                size_t p = req.body.find("\"" + k + "\"");
                if (p == std::string::npos) return -1;
                p = req.body.find(':', p);
                if (p == std::string::npos) return -1;
                return static_cast<int>(std::strtol(req.body.c_str() + p + 1, nullptr, 10));
            };
            const int idx = find_int("hole_aim_ball_index");
            hole_aim_ball_index_.store(idx);
            hole_aim_ball_index_set_.store(true);
            res.set_content("{\"ok\":true,\"hole_aim_ball_index\":" + std::to_string(idx) + "}", "application/json");
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
            // Preserve target hole from any existing entry for this ball (anonymous or named)
            int preserved_target_hole = -1;
            for (const auto& u : users_) {
                if (u.ball_index == ball_index && u.target_hole_index >= 0) {
                    preserved_target_hole = u.target_hole_index;
                    break;
                }
            }
            // Clear ball_index from all entries
            for (auto& u : users_) {
                if (u.ball_index == ball_index) u.ball_index = -1;
            }
            // Remove anonymous zombies (no username, no ball)
            users_.erase(std::remove_if(users_.begin(), users_.end(),
                             [](const UserState& x) { return x.username.empty() && x.ball_index < 0; }),
                         users_.end());
            // Remove existing entry for this session+username (may override preserved target)
            users_.erase(std::remove_if(users_.begin(), users_.end(),
                             [&](const UserState& x) {
                                 if (x.session_id == session_id && x.username == username) {
                                     if (x.target_hole_index >= 0) preserved_target_hole = x.target_hole_index;
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
            nu.target_hole_anchor_valid = false;
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

void StatsApi::refresh_target_hole_remap(const std::vector<HolePos>& holes) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    constexpr float kMaxSnapPx = 200.f;
    const float max_d2 = kMaxSnapPx * kMaxSnapPx;

    for (auto& u : users_) {
        if (u.target_hole_index < 0) {
            u.target_hole_anchor_valid = false;
            continue;
        }
        if (holes.empty()) continue;

        if (!u.target_hole_anchor_valid) {
            const size_t idx = static_cast<size_t>(u.target_hole_index);
            if (idx < holes.size()) {
                u.target_hole_anchor_x = holes[idx].x;
                u.target_hole_anchor_y = holes[idx].y;
                u.target_hole_anchor_valid = true;
            }
            continue;
        }

        int best_i = -1;
        float best_d2 = 1e30f;
        for (size_t i = 0; i < holes.size(); ++i) {
            const HolePos& hp = holes[i];
            if (!hp.valid) continue;
            const float dx = hp.x - u.target_hole_anchor_x;
            const float dy = hp.y - u.target_hole_anchor_y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_i = static_cast<int>(i);
            }
        }
        if (best_i >= 0 && best_d2 <= max_d2) {
            u.target_hole_index = best_i;
        }
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
    (void)total_balls;
    // Do not attribute a claimed username to a different stable_id (single-ball fallback was wrong with 2+ balls).
    return get_username_for_ball(ball_index);
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
            u.ball_track_visible = true;
            // After a make, the ball is often visible in the cup — do not clear "return to start" until the ball
            // is seen again near the putt start; otherwise last_known becomes the cup and the marker jumps to the hole.
            if (u.placement_return_after_putt && u.placement_putt_start_valid) {
                const float dx = tr->x - u.placement_putt_start_x;
                const float dy = tr->y - u.placement_putt_start_y;
                const float d2 = dx * dx + dy * dy;
                // Slightly larger than reclaim snap radius so the same ball can clear "return" after a valid snap.
                constexpr float kReturnNearStartPx = 130.f;
                if (d2 <= kReturnNearStartPx * kReturnNearStartPx) {
                    u.placement_return_after_putt = false;
                    u.placement_putt_start_valid = false;
                }
            } else {
                u.placement_return_after_putt = false;
                u.placement_putt_start_valid = false;
            }
        }
    }
}

void StatsApi::notify_putt_made_for_stable_id(int stable_id, float start_x, float start_y) {
    if (stable_id < 0) return;
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (auto& u : users_) {
        if (u.ball_index == stable_id && !u.username.empty()) {
            u.placement_return_after_putt = true;
            u.placement_putt_start_x = start_x;
            u.placement_putt_start_y = start_y;
            u.placement_putt_start_valid = true;
            // #region agent log (debug a3d342)
            {
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                std::ofstream dbg("../../debug-a3d342.log", std::ios::app);
                if (dbg.is_open()) {
                    dbg << "{\"sessionId\":\"a3d342\",\"hypothesisId\":\"H_PLACE\",\"location\":\"StatsApi::notify_putt_made\",\"message\":\"putt_start_captured\",\"data\":{\"stable_id\":" << stable_id << ",\"start_x\":" << start_x << ",\"start_y\":" << start_y << "},\"timestamp\":" << ms << "}\n";
                }
            }
            // #endregion agent log (debug a3d342)
            return;
        }
    }
    // #region agent log (debug a3d342)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream dbg("../../debug-a3d342.log", std::ios::app);
        if (dbg.is_open()) {
            dbg << "{\"sessionId\":\"a3d342\",\"hypothesisId\":\"H_NOTIFY_MISS\",\"location\":\"StatsApi::notify_putt_made\",\"message\":\"no_user_for_stable_id\",\"data\":{\"stable_id\":" << stable_id << "},\"timestamp\":" << ms << "}\n";
        }
    }
    // #endregion agent log (debug a3d342)
}

void StatsApi::try_reassign_placement_return_near_hint(const std::vector<TrackedObject>& balls,
                                                       float max_dist_px) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    const float max_d2 = max_dist_px * max_dist_px;
    const float min_gap = kPlacementReclaimUnambiguousGapPx;

    // If more than one ball track is visible, the nearest ball to the return hint is often *not* the returning
    // player’s ball (stray unclaimed ball on the mat while the real ball is still at the cup). Auto-snap then
    // reassigns the username to the wrong track (e.g. pillarman38). Only allow reclaim when a single ball exists.
    size_t n_valid_balls = 0;
    for (const auto& b : balls) {
        if (b.valid) {
            ++n_valid_balls;
        }
    }
    if (n_valid_balls > 1) {
        // #region agent log (debug a3d342)
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::ofstream dbg("../../debug-a3d342.log", std::ios::app);
            if (dbg.is_open()) {
                dbg << "{\"sessionId\":\"a3d342\",\"hypothesisId\":\"H_RECLAIM_SKIP\",\"location\":\"StatsApi::try_reassign_placement_return_near_hint\",\"message\":\"multi_ball_no_auto_snap\",\"data\":{\"n_valid_balls\":" << n_valid_balls << "},\"timestamp\":" << ms << "}\n";
            }
        }
        // #endregion agent log (debug a3d342)
        return;
    }

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
        // Only snap claim for made-putt return-line hints. Occlusion / pickup uses last-known pixels; the nearest
        // *other* ball within range is often unrelated — reassigning there stole the username and cleared markers.
        if (!u.placement_after_putt) continue;
        // Made-putt line: reclaimed ball often gets a new stable_id while the reserved ghost stays invalid —
        // snapping the claim clears placement_waiting when the real ball is seen again.
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

    std::vector<bool> cand_used(cands.size(), false);

    for (const auto& w : wants) {
        struct Scored {
            float d2;
            size_t ci;
        };
        std::vector<Scored> in_range;
        for (size_t ci = 0; ci < cands.size(); ++ci) {
            if (cand_used[ci]) continue;
            const auto& b = balls[cands[ci].ball_i];
            const float dx = b.x - w.px;
            const float dy = b.y - w.py;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= max_d2) {
                in_range.push_back({d2, ci});
            }
        }
        if (in_range.empty()) continue;
        std::sort(in_range.begin(), in_range.end(),
                  [](const Scored& a, const Scored& b) { return a.d2 < b.d2; });

        const float r1 = std::sqrt(in_range[0].d2);
        if (in_range.size() >= 2) {
            const float r2 = std::sqrt(in_range[1].d2);
            if (r2 - r1 < min_gap) {
                // #region agent log (debug a3d342)
                {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    std::ofstream dbg("../../debug-a3d342.log", std::ios::app);
                    if (dbg.is_open()) {
                        dbg << "{\"sessionId\":\"a3d342\",\"hypothesisId\":\"H_RECLAIM_SKIP\",\"location\":\"StatsApi::try_reassign_placement_return_near_hint\",\"message\":\"ambiguous_reclaim\",\"data\":{\"r1\":" << r1 << ",\"r2\":" << r2 << ",\"min_gap\":" << min_gap << "},\"timestamp\":" << ms << "}\n";
                    }
                }
                // #endregion agent log (debug a3d342)
                continue;
            }
        }

        const size_t best_ci = in_range[0].ci;
        const int new_sid = cands[best_ci].stable_id;
        const size_t user_i = w.user_i;
        const int old_bi = users_[user_i].ball_index;
        if (old_bi == new_sid) continue;

        users_[user_i].ball_index = new_sid;
        cand_used[best_ci] = true;

        // #region agent log (debug a3d342)
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::ofstream dbg("../../debug-a3d342.log", std::ios::app);
            if (dbg.is_open()) {
                dbg << "{\"sessionId\":\"a3d342\",\"hypothesisId\":\"H_RECLAIM\",\"location\":\"StatsApi::try_reassign_placement_return_near_hint\",\"message\":\"ball_index_reassigned\",\"data\":{\"old_bi\":" << old_bi << ",\"new_sid\":" << new_sid << ",\"d2\":" << in_range[0].d2 << ",\"max_dist_px\":" << max_dist_px << "},\"timestamp\":" << ms << "}\n";
            }
        }
        // #endregion agent log (debug a3d342)
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

    // Show "return ball here" at putt start as soon as the make is registered — not only after the track goes
    // invisible. While the ball is still visible in the cup, ball_track_visible is true; gating on !visible
    // meant line_idxs stayed empty and no PlaceBallMarker / Contour hint ever appeared.
    std::vector<size_t> line_idxs;
    for (size_t i = 0; i < users_.size(); ++i) {
        const UserState& u = users_[i];
        if (u.username.empty() || u.ball_index < 0) continue;
        if (u.placement_return_after_putt) {
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
            // After a make, anchor the return marker at putt start — not green/hole centroid (often near cup).
            if (u.placement_putt_start_valid) {
                u.placement_pixel_x = u.placement_putt_start_x + offset;
                u.placement_pixel_y = u.placement_putt_start_y;
            } else {
                u.placement_pixel_x = green_center_x + offset;
                u.placement_pixel_y = green_center_y;
            }
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

    // #region agent log (debug a3d342)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream dbg("../../debug-a3d342.log", std::ios::app);
        if (dbg.is_open() && !line_idxs.empty()) {
            const UserState& u0 = users_[line_idxs[0]];
            dbg << "{\"sessionId\":\"a3d342\",\"hypothesisId\":\"H_PLACE\",\"location\":\"StatsApi::finalize_placement_hints\",\"message\":\"after_putt_anchor\",\"data\":{\"n_line\":" << n_line << ",\"anchor\":\"" << (u0.placement_putt_start_valid ? "putt_start" : "green_center") << "\",\"px\":" << u0.placement_pixel_x << ",\"py\":" << u0.placement_pixel_y << ",\"green_cx\":" << green_center_x << ",\"green_cy\":" << green_center_y << "},\"timestamp\":" << ms << "}\n";
        }
    }
    // #endregion agent log (debug a3d342)
}

}  // namespace golf
