#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// tracker.h  –  Simple Ball & Putter Tracker
//
// Uses a lightweight exponential-moving-average (EMA) tracker to smooth
// positions and estimate velocity.  No external tracking library needed.
// ─────────────────────────────────────────────────────────────────────────────

#include "frame_pipeline.h"

#include <chrono>
#include <deque>
#include <optional>
#include <vector>

namespace golf {

/// Smoothed state for a tracked object.
struct TrackedObject {
    int class_id = -1;
    int stable_id = -1;               // stable identity; recycled when ball disappears, reassigned to next ball
    float x = 0.f, y = 0.f;          // smoothed center position (px)
    float vx = 0.f, vy = 0.f;        // estimated velocity (px / s)
    float confidence = 0.f;
    int frames_since_seen = 0;
    bool valid = false;
};

struct HolePos {
    int class_id = -1;
    float x = 0.f, y = 0.f, radius = 0.f;
    bool valid = false;
    int frames_since_seen = 0; 
};

// ─── Tracker ────────────────────────────────────────────────────────────────
class Tracker {
public:
    /// @param alpha       EMA smoothing factor (0-1, higher = more responsive)
    /// @param max_lost    frames before a track is considered lost
    explicit Tracker(float alpha = 0.6f, int max_lost = 15);

    float min_dist_px = 999999.0f;

    /// Feed new detections from the current frame.
    void update(const std::vector<Detection>& detections, double dt_seconds);

    bool is_putt_made = false;
    void reset_putt() { is_putt_made = false; }

    /// Call when a new putt starts (ball in motion) so "Putt ended" can fire again next time ball is lost.
    void reset_for_new_putt() { putt_ended_fired_ = false; }

    /// Set target hole index from UI (-1 = auto-select closest to ball). When valid, overrides auto-selection.
    void set_target_hole_index(int idx) { target_hole_index_ = idx; }

    /// Retrieve the current ball state (class_id == 0). First ball for backward compat.
    const TrackedObject& ball() const { return balls_.empty() ? empty_ball_ : balls_[0]; }

    /// All tracked balls (position-sorted for stable indices).
    const std::vector<TrackedObject>& balls() const { return balls_; }

    /// Retrieve the current putter state (class_id == 1).
    const TrackedObject& putter() const { return putter_; }

    /// True when any ball track is active.
    bool ball_visible() const { return !balls_.empty() && balls_[0].valid; }

    /// True when the putter track is active.
    bool putter_visible() const { return putter_.valid; }

    /// Retrieve the current hole position (primary hole used for PPI / putt-made).
    const HolePos& hole_pos() const { return hole_pos_; }

    /// Index of primary hole in holes() (-1 if none).
    int primary_hole_index() const { return primary_hole_index_; }

    /// All detected holes this frame (updates when camera moves, like ball).
    const std::vector<HolePos>& holes() const { return holes_; }

private:
    void update_track(TrackedObject& track, const Detection* det, double dt);

    float alpha_;
    int max_lost_;

    std::vector<TrackedObject> balls_;  // position-sorted for stable indices
    TrackedObject empty_ball_;          // invalid fallback for ball()
    TrackedObject putter_;
    std::vector<HolePos> holes_;   // all holes this frame
    HolePos hole_pos_;             // primary hole (for PPI / putt-made)
    bool putt_ended_fired_ = false; // prevents repeated "Putt ended" when camera loses/regains ball
    int target_hole_index_ = -1;   // from UI; -1 = auto-select
    float last_primary_x_ = -1.f, last_primary_y_ = -1.f;  // sticky selection: don't switch when new hole appears
    int frames_without_holes_ = 0;
    int primary_hole_index_ = 0;
    static constexpr int kMaxFramesWithoutHoles = 150;

    int next_stable_id_ = 0;
    std::deque<int> recycled_stable_ids_;  // FIFO: next new ball gets front
};

}  // namespace golf
