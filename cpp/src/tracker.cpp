// ─────────────────────────────────────────────────────────────────────────────
// tracker.cpp  –  Ball & Putter EMA Tracker
// ─────────────────────────────────────────────────────────────────────────────

#include "tracker.h"

#include <cmath>
#include <limits>

namespace golf {

Tracker::Tracker(float alpha, int max_lost)
    : alpha_(alpha), max_lost_(max_lost) {
    ball_.class_id = 0;
    putter_.class_id = 1;
}

void Tracker::update(const std::vector<Detection>& detections, double dt) {
    // Find best detection for each class (highest confidence)
    const Detection* best_ball = nullptr;
    const Detection* best_putter = nullptr;
    float best_ball_conf = 0.f;
    float best_putter_conf = 0.f;

    for (const auto& d : detections) {
        if (d.class_id == 0 && d.confidence > best_ball_conf) {
            best_ball = &d;
            best_ball_conf = d.confidence;
        } else if (d.class_id == 1 && d.confidence > best_putter_conf) {
            best_putter = &d;
            best_putter_conf = d.confidence;
        }
    }

    update_track(ball_, best_ball, dt);
    update_track(putter_, best_putter, dt);
}

void Tracker::update_track(TrackedObject& track, const Detection* det,
                           double dt) {
    if (det) {
        float new_x = det->cx();
        float new_y = det->cy();

        if (!track.valid) {
            // First detection – snap to position
            track.x = new_x;
            track.y = new_y;
            track.vx = 0.f;
            track.vy = 0.f;
        } else {
            // EMA position update
            float prev_x = track.x;
            float prev_y = track.y;

            track.x = alpha_ * new_x + (1.f - alpha_) * track.x;
            track.y = alpha_ * new_y + (1.f - alpha_) * track.y;

            // Velocity estimate (px / s)
            if (dt > 1e-6) {
                float inst_vx = (track.x - prev_x) / static_cast<float>(dt);
                float inst_vy = (track.y - prev_y) / static_cast<float>(dt);
                track.vx = alpha_ * inst_vx + (1.f - alpha_) * track.vx;
                track.vy = alpha_ * inst_vy + (1.f - alpha_) * track.vy;
            }
        }

        track.confidence = det->confidence;
        track.frames_since_seen = 0;
        track.valid = true;
    } else {
        // No detection this frame
        track.frames_since_seen++;
        if (track.frames_since_seen > max_lost_) {
            track.valid = false;
            track.vx = 0.f;
            track.vy = 0.f;
        } else if (track.valid) {
            // Coast using last velocity
            track.x += track.vx * static_cast<float>(dt);
            track.y += track.vy * static_cast<float>(dt);

            // Decay velocity
            track.vx *= 0.9f;
            track.vy *= 0.9f;
        }
    }
}

}  // namespace golf
