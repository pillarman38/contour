// ─────────────────────────────────────────────────────────────────────────────
// unreal_sender.cpp  –  UDP JSON Sender for Unreal Engine
// ─────────────────────────────────────────────────────────────────────────────

#include "unreal_sender.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace golf {

UnrealSender::~UnrealSender() {
    close();
}

bool UnrealSender::init(const std::string& host, uint16_t port) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[UnrealSender] WSAStartup failed\n";
        return false;
    }
#endif

    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ == invalid_socket) {
        std::cerr << "[UnrealSender] socket() failed\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    dest_addr_ = new sockaddr_in{};
    dest_addr_->sin_family = AF_INET;
    dest_addr_->sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &dest_addr_->sin_addr) <= 0) {
        std::cerr << "[UnrealSender] Invalid address: " << host << "\n";
        close();
        return false;
    }

    std::cout << "[UnrealSender] Sending to " << host << ":" << port << "\n";
    return true;
}

static void escape_json_str(std::string& out, const std::string& s) {
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
}

bool UnrealSender::send(const std::vector<BallPayload>& balls,
                        const TrackedObject& putter,
                        const std::vector<TrackedObject>& putters_all,
                        const std::vector<HolePos>& holes,
                        int target_hole_index,
                        float target_hole_x, float target_hole_y,
                        const std::vector<BallPlacementHint>& placement_hints,
                        bool hole_aim_ball_index_set,
                        int hole_aim_ball_index) {
    if (sock_fd_ == invalid_socket) return false;

    auto now = std::chrono::steady_clock::now();
    uint64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();

    std::string buf;
    buf.reserve(4096);
    buf += "{\"timestamp_ms\":";
    buf += std::to_string(ts_ms);
    buf += ",\"ball\":";
    if (!balls.empty() && balls[0].ball) {
        const auto& b = *balls[0].ball;
        char tmp[256];
        std::snprintf(tmp, sizeof(tmp),
            "{\"x\":%.2f,\"y\":%.2f,\"vx\":%.2f,\"vy\":%.2f,\"conf\":%.3f,\"visible\":%s}",
            b.x, b.y, b.vx, b.vy, b.confidence, b.valid ? "true" : "false");
        buf += tmp;
    } else {
        buf += "{\"x\":0,\"y\":0,\"vx\":0,\"vy\":0,\"conf\":0,\"visible\":false}";
    }
    buf += ",\"balls\":[";
    for (size_t i = 0; i < balls.size(); ++i) {
        if (i > 0) buf += ",";
        const auto& bp = balls[i];
        if (!bp.ball) { buf += "{}"; continue; }
        const auto& b = *bp.ball;
        char tmp[640];
        std::snprintf(tmp, sizeof(tmp),
            "{\"x\":%.2f,\"y\":%.2f,\"vx\":%.2f,\"vy\":%.2f,\"conf\":%.3f,\"visible\":%s,"
            "\"stable_id\":%d,\"username\":\"",
            b.x, b.y, b.vx, b.vy, b.confidence, b.valid ? "true" : "false", b.stable_id);
        buf += tmp;
        escape_json_str(buf, bp.username);
        std::snprintf(tmp, sizeof(tmp),
            "\",\"target_hole_index\":%d,\"target_hole_x\":%.2f,\"target_hole_y\":%.2f"
            ",\"stats\":{\"putt_number\":%d,\"state\":\"%s\","
            "\"launch_speed\":%.2f,\"current_speed\":%.2f,\"peak_speed\":%.2f,"
            "\"total_distance\":%.2f,\"break_distance\":%.2f,\"time_in_motion\":%.2f,"
            "\"start_x\":%.2f,\"start_y\":%.2f,\"peak_speed_x\":%.2f,\"peak_speed_y\":%.2f,"
            "\"final_x\":%.2f,\"final_y\":%.2f"
            "},\"putt_made\":%s}",
            bp.target_hole_index, bp.target_hole_x, bp.target_hole_y,
            bp.stats.putt_number, bp.stats.state_str(),
            bp.stats.launch_speed, bp.stats.current_speed, bp.stats.peak_speed,
            bp.stats.total_distance, bp.stats.break_distance, bp.stats.time_in_motion,
            bp.stats.start_x, bp.stats.start_y,
            bp.stats.peak_speed_x, bp.stats.peak_speed_y,
            bp.stats.final_x, bp.stats.final_y,
            bp.is_putt_made ? "true" : "false");
        buf += tmp;
    }
    buf += "],\"putter\":{";
    char tmp[512];  // was 128 - stats block alone needs ~350+ chars
    std::snprintf(tmp, sizeof(tmp),
        "\"x\":%.2f,\"y\":%.2f,\"vx\":%.2f,\"vy\":%.2f,\"conf\":%.3f,\"visible\":%s}",
        putter.x, putter.y, putter.vx, putter.vy,
        putter.confidence, putter.valid ? "true" : "false");
    buf += tmp;
    buf += ",\"putters\":[";
    for (size_t i = 0; i < putters_all.size(); ++i) {
        if (i > 0) buf += ",";
        const auto& p = putters_all[i];
        std::snprintf(tmp, sizeof(tmp),
            "{\"x\":%.2f,\"y\":%.2f,\"vx\":%.2f,\"vy\":%.2f,\"conf\":%.3f,\"visible\":%s}",
            p.x, p.y, p.vx, p.vy, p.confidence, p.valid ? "true" : "false");
        buf += tmp;
    }
    buf += "],\"holes\":[";
    for (size_t i = 0; i < holes.size(); ++i) {
        if (i > 0) buf += ",";
        std::snprintf(tmp, sizeof(tmp),
            "{\"x\":%.2f,\"y\":%.2f,\"radius\":%.2f,\"visible\":%s}",
            holes[i].x, holes[i].y, holes[i].radius,
            holes[i].valid ? "true" : "false");
        buf += tmp;
    }
    buf += "],\"ball_placements\":[";
    for (size_t i = 0; i < placement_hints.size(); ++i) {
        if (i > 0) buf += ",";
        buf += "{\"username\":\"";
        escape_json_str(buf, placement_hints[i].username);
        std::snprintf(tmp, sizeof(tmp),
            "\",\"stable_id\":%d,\"pixel_x\":%.2f,\"pixel_y\":%.2f,\"waiting\":%s,\"after_putt\":%s}",
            placement_hints[i].stable_id,
            placement_hints[i].pixel_x,
            placement_hints[i].pixel_y,
            placement_hints[i].waiting ? "true" : "false",
            placement_hints[i].after_putt ? "true" : "false");
        buf += tmp;
    }
    buf += "]";
    if (hole_aim_ball_index_set) {
        buf += ",\"hole_aim_ball_index\":";
        buf += std::to_string(hole_aim_ball_index);
    }
    int ti = (target_hole_index >= 0 && static_cast<size_t>(target_hole_index) < holes.size())
             ? target_hole_index : 0;
    std::snprintf(tmp, sizeof(tmp),
        ",\"target_hole_index\":%d,\"target_hole_x\":%.2f,\"target_hole_y\":%.2f,"
        "\"stats\":{"
        "\"putt_number\":%d,\"state\":\"%s\",\"launch_speed\":%.2f,\"current_speed\":%.2f,"
        "\"peak_speed\":%.2f,\"total_distance\":%.2f,\"break_distance\":%.2f,\"time_in_motion\":%.2f,"
        "\"start_x\":%.2f,\"start_y\":%.2f,\"peak_speed_x\":%.2f,\"peak_speed_y\":%.2f,"
        "\"final_x\":%.2f,\"final_y\":%.2f"
        "},\"putt_made\":%s}",
        ti, target_hole_x, target_hole_y,
        balls.empty() ? 0 : balls[0].stats.putt_number,
        balls.empty() ? "idle" : balls[0].stats.state_str(),
        balls.empty() ? 0.f : balls[0].stats.launch_speed,
        balls.empty() ? 0.f : balls[0].stats.current_speed,
        balls.empty() ? 0.f : balls[0].stats.peak_speed,
        balls.empty() ? 0.f : balls[0].stats.total_distance,
        balls.empty() ? 0.f : balls[0].stats.break_distance,
        balls.empty() ? 0.f : balls[0].stats.time_in_motion,
        balls.empty() ? 0.f : balls[0].stats.start_x,
        balls.empty() ? 0.f : balls[0].stats.start_y,
        balls.empty() ? 0.f : balls[0].stats.peak_speed_x,
        balls.empty() ? 0.f : balls[0].stats.peak_speed_y,
        balls.empty() ? 0.f : balls[0].stats.final_x,
        balls.empty() ? 0.f : balls[0].stats.final_y,
        (balls.empty() ? false : balls[0].is_putt_made) ? "true" : "false");
    buf += tmp;

    int sent = sendto(sock_fd_, buf.data(), static_cast<int>(buf.size()), 0,
                      reinterpret_cast<sockaddr*>(dest_addr_),
                      sizeof(*dest_addr_));
    if (sent < 0) {
        std::cerr << "[UnrealSender] sendto() failed\n";
        return false;
    }
    return true;
}

bool UnrealSender::send(const TrackedObject& ball, const TrackedObject& putter,
                        const std::vector<HolePos>& holes,
                        const PuttData& stats, bool is_putt_made,
                        int target_hole_index,
                        float target_hole_x, float target_hole_y) {
    BallPayload bp;
    bp.ball = &ball;
    bp.stats = stats;
    bp.is_putt_made = is_putt_made;
    std::vector<BallPayload> balls = { bp };
    return send(balls, putter, {}, holes, target_hole_index, target_hole_x, target_hole_y, {}, false, -1);
}

void UnrealSender::close() {
    if (sock_fd_ != invalid_socket) {
#ifdef _WIN32
        closesocket(sock_fd_);
        WSACleanup();
#else
        ::close(sock_fd_);
#endif
        sock_fd_ = invalid_socket;
    }
    delete dest_addr_;
    dest_addr_ = nullptr;
}

}  // namespace golf
