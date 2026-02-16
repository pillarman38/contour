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
// }
// ─────────────────────────────────────────────────────────────────────────────

#include "tracker.h"

#include <cstdint>
#include <string>

namespace golf {

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

    /// Send the current tracker state as a JSON datagram.
    bool send(const TrackedObject& ball, const TrackedObject& putter);

    /// Close the socket.
    void close();

private:
    int sock_fd_ = -1;
    struct sockaddr_in* dest_addr_ = nullptr;
};

}  // namespace golf
