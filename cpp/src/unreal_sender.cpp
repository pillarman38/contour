// ─────────────────────────────────────────────────────────────────────────────
// unreal_sender.cpp  –  UDP JSON Sender for Unreal Engine
// ─────────────────────────────────────────────────────────────────────────────

#include "unreal_sender.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace golf {

UnrealSender::~UnrealSender() {
    close();
}

bool UnrealSender::init(const std::string& host, uint16_t port) {
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ < 0) {
        std::cerr << "[UnrealSender] socket() failed\n";
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

bool UnrealSender::send(const TrackedObject& ball, const TrackedObject& putter) {
    if (sock_fd_ < 0) return false;

    auto now = std::chrono::steady_clock::now();
    uint64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();

    // Build JSON payload (hand-rolled to avoid external dependency)
    char buf[512];
    int n = std::snprintf(buf, sizeof(buf),
        "{"
            "\"timestamp_ms\":%" PRIu64 ","
            "\"ball\":{"
                "\"x\":%.2f,\"y\":%.2f,"
                "\"vx\":%.2f,\"vy\":%.2f,"
                "\"conf\":%.3f,\"visible\":%s"
            "},"
            "\"putter\":{"
                "\"x\":%.2f,\"y\":%.2f,"
                "\"vx\":%.2f,\"vy\":%.2f,"
                "\"conf\":%.3f,\"visible\":%s"
            "}"
        "}",
        ts_ms,
        ball.x, ball.y, ball.vx, ball.vy,
        ball.confidence, ball.valid ? "true" : "false",
        putter.x, putter.y, putter.vx, putter.vy,
        putter.confidence, putter.valid ? "true" : "false");

    if (n < 0 || n >= static_cast<int>(sizeof(buf))) {
        std::cerr << "[UnrealSender] JSON format error\n";
        return false;
    }

    ssize_t sent = sendto(sock_fd_, buf, n, 0,
                          reinterpret_cast<sockaddr*>(dest_addr_),
                          sizeof(*dest_addr_));
    if (sent < 0) {
        std::cerr << "[UnrealSender] sendto() failed\n";
        return false;
    }
    return true;
}

void UnrealSender::close() {
    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
    delete dest_addr_;
    dest_addr_ = nullptr;
}

}  // namespace golf
