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
#include <unordered_set>
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

/// Provisional cup detections; promoted into \ref holes_ after continuous visibility exceeds the confirm threshold.
struct PendingHole {
    float x = 0.f, y = 0.f, radius = 0.f;
    float seconds_visible = 0.f;
    int frames_lost_streak = 0;
    bool seen_this_frame = false;
};

// ─── Tracker ────────────────────────────────────────────────────────────────
class Tracker {
public:
    /// @param alpha       EMA smoothing factor (0-1, higher = more responsive)
    /// @param max_lost    frames before a track is considered lost
    explicit Tracker(float alpha = 0.6f, int max_lost = 45);

    float min_dist_px = 999999.0f;

    /// Feed new detections from the current frame.
    void update(const std::vector<Detection>& detections, double dt_seconds);

    bool is_putt_made = false;
    /** Set with \c is_putt_made: stable_id of the ball track that was lost at the hole (for StatsApi / claims). */
    int putt_made_ball_stable_id = -1;
    void reset_putt() {
        is_putt_made = false;
        putt_made_ball_stable_id = -1;
    }

    /// Call when a new putt starts (ball in motion) so "Putt ended" can fire again next time ball is lost.
    void reset_for_new_putt() { putt_ended_fired_ = false; }

    /// Set target hole index from UI (-1 = auto-select closest to ball). When valid, overrides auto-selection.
    void set_target_hole_index(int idx) { target_hole_index_ = idx; }

    /// Stable IDs that must not be recycled while the track is lost (e.g. user claimed this ball in the app).
    /// Cleared automatically if not set each frame before \ref update.
    void set_reserved_stable_ids(std::unordered_set<int> ids) { reserved_stable_ids_ = std::move(ids); }

    /// Retrieve the current ball state (class_id == 0). First ball for backward compat.
    const TrackedObject& ball() const { return balls_.empty() ? empty_ball_ : balls_[0]; }

    /// All tracked balls (position-sorted for stable indices).
    const std::vector<TrackedObject>& balls() const { return balls_; }

    /// Retrieve the current putter state (class_id == 1). Best-confidence single putter for legacy/UE.
    const TrackedObject& putter() const { return putter_; }

    /// All putter detections this frame (no stable-ID tracking; raw per-frame positions).
    const std::vector<TrackedObject>& putters() const { return putters_; }

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
    TrackedObject putter_;              // best-confidence single putter (legacy/UE)
    std::vector<TrackedObject> putters_; // all putter detections this frame
    std::vector<HolePos> holes_;       // confirmed holes only (see PendingHole)
    std::vector<PendingHole> pending_holes_;
    HolePos hole_pos_;                 // primary hole (for PPI / putt-made)
    bool putt_ended_fired_ = false; // prevents repeated "Putt ended" when camera loses/regains ball
    int target_hole_index_ = -1;   // from UI; -1 = auto-select
    float last_primary_x_ = -1.f, last_primary_y_ = -1.f;  // sticky selection: don't switch when new hole appears
    int frames_without_holes_ = 0;
    int primary_hole_index_ = 0;
    static constexpr int kMaxFramesWithoutHoles = 150;

    int next_stable_id_ = 0;
    std::deque<int> recycled_stable_ids_;  // FIFO: next new ball gets front
    std::unordered_set<int> reserved_stable_ids_;
};

}  // namespace golf
