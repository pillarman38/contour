#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// unreal_sender.h  –  Send Detection Results to Unreal Engine over UDP
//
// Protocol:  JSON datagrams sent to a configurable UDP endpoint.
//
// Payload schema (one datagram per frame):
// {
//   "timestamp_ms": <uint64>,
//   "ball": { "x": <f>, "y": <f>, "vx": <f>, "vy": <f>, "conf": <f>, "visible": <bool> },
//   "putter": { "x": <f>, "y": <f>, "vx": <f>, "vy": <f>, "conf": <f>, "visible": <bool> }
//   "holes": [ { "x": <f>, "y": <f>, "radius": <f>, "visible": <bool> }, ... ],
//   "balls": [ { "x": <f>, ... "stable_id": <int>, "username": "...", ... }, ... ],
//   "ball_placements": [ { "username": "...", "stable_id": <int>, "pixel_x": <f>, "pixel_y": <f>, "waiting": <bool>, "after_putt": <bool> }, ... ]
// }
// ─────────────────────────────────────────────────────────────────────────────

#include "tracker.h"
#include "putt_stats.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace golf {

/// Where a claimed player should return their ball (camera pixels), for Unreal placement markers.
struct BallPlacementHint {
    std::string username;
    int stable_id = -1;
    float pixel_x = 0.f;
    float pixel_y = 0.f;
    /** True while the ball is not visible (picked up / lost): show marker at pixel_* . */
    bool waiting = false;
    /** True when marker is on the return line after a made putt (green center); false = last-known occlusion. */
    bool after_putt = false;
};

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;
#endif

class UnrealSender {
public:
    UnrealSender() = default;
    ~UnrealSender();

    UnrealSender(const UnrealSender&) = delete;
    UnrealSender& operator=(const UnrealSender&) = delete;

    /// Initialise the UDP socket.
    /// @param host  destination IP (e.g. "127.0.0.1")
    /// @param port  destination port (e.g. 7001)
    bool init(const std::string& host, uint16_t port);

    /// Per-ball payload for multi-ball mode.
    struct BallPayload {
        const TrackedObject* ball = nullptr;
        std::string username;
        PuttData stats;
        bool is_putt_made = false;
        /** Per-claimed-ball aim target (pixels). Sent in JSON for multi-player aim lines in Unreal. */
        int target_hole_index = -1;
        float target_hole_x = 0.f;
        float target_hole_y = 0.f;
    };

    /// Send the current tracker state + putt stats as a JSON datagram.
    /// @param balls  all balls with username and stats (empty = use single-ball legacy)
    /// @param target_hole_index  hole index for ball-to-hole line (-1 = use 0)
    /// @param placement_hints  per-claimed-user return spots (JSON field ball_placements)
    bool send(const std::vector<BallPayload>& balls,
              const TrackedObject& putter,
              const std::vector<HolePos>& holes,
              int target_hole_index = 0,
              float target_hole_x = 0.f, float target_hole_y = 0.f,
              const std::vector<BallPlacementHint>& placement_hints = {});

    /// Legacy single-ball send (delegates to multi-ball with one entry).
    bool send(const TrackedObject& ball, const TrackedObject& putter,
              const std::vector<HolePos>& holes,
              const PuttData& stats, bool is_putt_made,
              int target_hole_index = 0,
              float target_hole_x = 0.f, float target_hole_y = 0.f);

    /// Close the socket.
    void close();

private:
    socket_t sock_fd_ = invalid_socket;
    ::sockaddr_in* dest_addr_ = nullptr;
};

}  // namespace golf
