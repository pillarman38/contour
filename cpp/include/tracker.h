#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// tracker.h  –  Simple Ball & Putter Tracker
//
// Uses a lightweight exponential-moving-average (EMA) tracker to smooth
// positions and estimate velocity.  No external tracking library needed.
// ─────────────────────────────────────────────────────────────────────────────

#include "frame_pipeline.h"

#include <chrono>
#include <optional>
#include <vector>

namespace golf {

/// Smoothed state for a tracked object.
struct TrackedObject {
    int class_id = -1;
    float x = 0.f, y = 0.f;          // smoothed center position (px)
    float vx = 0.f, vy = 0.f;        // estimated velocity (px / s)
    float confidence = 0.f;
    int frames_since_seen = 0;
    bool valid = false;
};

// ─── Tracker ────────────────────────────────────────────────────────────────
class Tracker {
public:
    /// @param alpha       EMA smoothing factor (0-1, higher = more responsive)
    /// @param max_lost    frames before a track is considered lost
    explicit Tracker(float alpha = 0.6f, int max_lost = 15);

    /// Feed new detections from the current frame.
    void update(const std::vector<Detection>& detections, double dt_seconds);

    /// Retrieve the current ball state (class_id == 0).
    const TrackedObject& ball() const { return ball_; }

    /// Retrieve the current putter state (class_id == 1).
    const TrackedObject& putter() const { return putter_; }

    /// True when the ball track is active.
    bool ball_visible() const { return ball_.valid; }

    /// True when the putter track is active.
    bool putter_visible() const { return putter_.valid; }

private:
    void update_track(TrackedObject& track, const Detection* det, double dt);

    float alpha_;
    int max_lost_;

    TrackedObject ball_;
    TrackedObject putter_;
};

}  // namespace golf
