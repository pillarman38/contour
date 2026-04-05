#include "tracker.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace golf {

static constexpr int kHoleClassId = 2;
static constexpr int kBallClassId = 0;
static constexpr int kPutterClassId = 1;
static constexpr size_t kMaxHoles = 8;
static constexpr size_t kMaxBalls = 4;
static constexpr float kHoleMatchDistPx = 80.f;   // detection within this dist = same hole
static constexpr float kBallMatchDistPx = 60.f;    // base match radius (px) for stationary / slow ball
static constexpr float kBallMatchSpeedScale = 6.f;   // extra radius += speed_px_per_s * dt * this (fast putts)
static constexpr float kBallMatchMaxPx = 450.f;    // cap so distant false detections do not steal tracks
static constexpr float kPutterOcclusionDistPx = 150.f; // putter-to-ball distance that counts as occlusion
static constexpr int kHoleMaxLostFrames = 150;     // keep hole this long when not seen
static constexpr float kHoleConfirmSeconds = 5.f;  // new detection must be seen this long before it becomes a hole
static constexpr int kPendingHoleMaxLostFrames = 5; // consecutive unmatched frames before dropping a pending hole

Tracker::Tracker(float alpha, int max_lost)
    : alpha_(alpha), max_lost_(max_lost) {
    empty_ball_.class_id = kBallClassId;
    empty_ball_.valid = false;
    hole_pos_.class_id = kHoleClassId;
    putter_.class_id = kPutterClassId;
}

void Tracker::update(const std::vector<Detection>& detections, double dt) {
    const Detection* best_putter = nullptr;
    float best_putter_conf = 0.f;

    // Collect all ball, putter, and hole detections
    std::vector<const Detection*> ball_dets;
    std::vector<const Detection*> putter_dets;
    std::vector<const Detection*> hole_dets;
    for (const auto& d : detections) {
        if (d.class_id == kBallClassId) {
            ball_dets.push_back(&d);
        } else if (d.class_id == kPutterClassId) {
            putter_dets.push_back(&d);
            if (d.confidence > best_putter_conf) {
                best_putter = &d;
                best_putter_conf = d.confidence;
            }
        } else if (d.class_id == kHoleClassId) {
            hole_dets.push_back(&d);
        }
    }
    std::sort(ball_dets.begin(), ball_dets.end(),
              [](const Detection* a, const Detection* b) { return a->confidence > b->confidence; });
    if (ball_dets.size() > kMaxBalls) ball_dets.resize(kMaxBalls);

    bool ball_was_valid = ball().valid;  // primary ball before update
    // Confirmed holes: match detections by position. New detections go to pending_holes_; they only enter holes_
    // after kHoleConfirmSeconds of continuous visibility. If a pending track is lost for more than
    // kPendingHoleMaxLostFrames before confirmation, it is discarded.
    std::sort(hole_dets.begin(), hole_dets.end(),
              [](const Detection* a, const Detection* b) { return a->confidence > b->confidence; });
    if (hole_dets.size() > kMaxHoles) hole_dets.resize(kMaxHoles);

    std::vector<bool> hole_matched(holes_.size(), false);
    std::vector<bool> det_used(hole_dets.size(), false);

    for (size_t di = 0; di < hole_dets.size(); ++di) {
        const Detection* d = hole_dets[di];
        float dx = d->cx();
        float dy = d->cy();
        float radius = (d->width() + d->height()) / 4.0f;
        float best_d2 = kHoleMatchDistPx * kHoleMatchDistPx;
        int best_idx = -1;
        for (size_t i = 0; i < holes_.size(); ++i) {
            if (hole_matched[i]) continue;
            float hx = holes_[i].x - dx;
            float hy = holes_[i].y - dy;
            float d2 = hx * hx + hy * hy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_idx >= 0) {
            HolePos& h = holes_[static_cast<size_t>(best_idx)];
            hole_matched[static_cast<size_t>(best_idx)] = true;
            det_used[di] = true;
            h.x = alpha_ * dx + (1.f - alpha_) * h.x;
            h.y = alpha_ * dy + (1.f - alpha_) * h.y;
            h.radius = radius;
            h.valid = true;
            h.frames_since_seen = 0;
        }
    }

    for (PendingHole& ph : pending_holes_) {
        ph.seen_this_frame = false;
    }

    const float dt_f = static_cast<float>(dt);

    for (size_t di = 0; di < hole_dets.size(); ++di) {
        if (det_used[di]) continue;
        const Detection* d = hole_dets[di];
        float dx = d->cx();
        float dy = d->cy();
        float radius = (d->width() + d->height()) / 4.0f;
        float best_d2 = kHoleMatchDistPx * kHoleMatchDistPx;
        int best_pi = -1;
        for (size_t i = 0; i < pending_holes_.size(); ++i) {
            if (pending_holes_[i].seen_this_frame) continue;
            float hx = pending_holes_[i].x - dx;
            float hy = pending_holes_[i].y - dy;
            float d2 = hx * hx + hy * hy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_pi = static_cast<int>(i);
            }
        }
        if (best_pi >= 0) {
            PendingHole& ph = pending_holes_[static_cast<size_t>(best_pi)];
            ph.seen_this_frame = true;
            ph.x = alpha_ * dx + (1.f - alpha_) * ph.x;
            ph.y = alpha_ * dy + (1.f - alpha_) * ph.y;
            ph.radius = radius;
            ph.seconds_visible += dt_f;
            ph.frames_lost_streak = 0;
        } else if (pending_holes_.size() < kMaxHoles) {
            PendingHole ph;
            ph.x = dx;
            ph.y = dy;
            ph.radius = radius;
            ph.seconds_visible = dt_f;
            ph.frames_lost_streak = 0;
            ph.seen_this_frame = true;
            pending_holes_.push_back(ph);
        }
    }

    for (size_t i = 0; i < pending_holes_.size();) {
        if (!pending_holes_[i].seen_this_frame) {
            pending_holes_[i].frames_lost_streak++;
            if (pending_holes_[i].frames_lost_streak > kPendingHoleMaxLostFrames) {
                pending_holes_.erase(pending_holes_.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
        }
        ++i;
    }

    for (int pi = static_cast<int>(pending_holes_.size()) - 1; pi >= 0; --pi) {
        const size_t psi = static_cast<size_t>(pi);
        PendingHole& ph = pending_holes_[psi];
        if (ph.seconds_visible < kHoleConfirmSeconds) continue;
        if (!ph.seen_this_frame) continue;
        HolePos h;
        h.class_id = kHoleClassId;
        h.x = ph.x;
        h.y = ph.y;
        h.radius = ph.radius;
        h.valid = true;
        h.frames_since_seen = 0;
        holes_.push_back(h);
        hole_matched.push_back(true);
        pending_holes_.erase(pending_holes_.begin() + static_cast<std::ptrdiff_t>(pi));
    }

    for (size_t i = 0; i < holes_.size(); ++i) {
        if (!hole_matched[i]) {
            holes_[i].frames_since_seen++;
            holes_[i].valid = false;
            if (holes_[i].frames_since_seen > kHoleMaxLostFrames) {
                holes_.erase(holes_.begin() + static_cast<std::ptrdiff_t>(i));
                hole_matched.erase(hole_matched.begin() + static_cast<std::ptrdiff_t>(i));
                --i;
            }
        }
    }
    if (holes_.size() > kMaxHoles) holes_.resize(kMaxHoles);
    // Stable sort by (x,y) so hole order doesn't change when visibility toggles
    std::sort(holes_.begin(), holes_.end(),
              [](const HolePos& a, const HolePos& b) {
                  if (a.x != b.x) return a.x < b.x;
                  return a.y < b.y;
              });

    // Multi-ball: match by position; widen search radius when the ball is moving fast so putts
    // do not drop the track (new stable_id => username / aim line lost in the app).
    std::vector<bool> ball_matched(balls_.size(), false);
    for (const Detection* d : ball_dets) {
        float dx = d->cx();
        float dy = d->cy();
        float best_d2 = std::numeric_limits<float>::infinity();
        int best_idx = -1;
        for (size_t i = 0; i < balls_.size(); ++i) {
            if (ball_matched[i]) continue;
            const TrackedObject& tr = balls_[i];
            float bx = tr.x - dx;
            float by = tr.y - dy;
            float d2 = bx * bx + by * by;
            float speed = std::sqrt(tr.vx * tr.vx + tr.vy * tr.vy);
            float max_dist = kBallMatchDistPx + speed * dt_f * kBallMatchSpeedScale;
            if (max_dist > kBallMatchMaxPx) max_dist = kBallMatchMaxPx;
            float thresh2 = max_dist * max_dist;
            if (d2 <= thresh2 && d2 < best_d2) {
                best_d2 = d2;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_idx >= 0) {
            ball_matched[best_idx] = true;
            update_track(balls_[best_idx], d, dt);
        } else {
            TrackedObject b;
            b.class_id = kBallClassId;
            // Recycle stable_id from a ball that disappeared (never recycle IDs still claimed in the app).
            while (!recycled_stable_ids_.empty() &&
                   reserved_stable_ids_.count(recycled_stable_ids_.front()) != 0u) {
                recycled_stable_ids_.pop_front();
            }
            if (!recycled_stable_ids_.empty()) {
                b.stable_id = recycled_stable_ids_.front();
                recycled_stable_ids_.pop_front();
            } else {
                b.stable_id = next_stable_id_++;
            }
            update_track(b, d, dt);
            balls_.push_back(b);
            ball_matched.push_back(true);
        }
    }
    for (size_t i = 0; i < balls_.size(); ++i) {
        if (!ball_matched[i]) {
            // Putter occlusion: if a putter is near this ball's last position, keep it
            // valid (the putter head / hand is hiding the ball from the camera).
            if (balls_[i].valid) {
                bool putter_occluding = false;
                for (const Detection* pd : putter_dets) {
                    float px = pd->cx() - balls_[i].x;
                    float py = pd->cy() - balls_[i].y;
                    if (px * px + py * py < kPutterOcclusionDistPx * kPutterOcclusionDistPx) {
                        putter_occluding = true;
                        break;
                    }
                }
                if (putter_occluding) {
                    balls_[i].frames_since_seen = 0;
                    continue;
                }
            }
            update_track(balls_[i], nullptr, dt);
            if (!balls_[i].valid) {
                const int sid = balls_[i].stable_id;
                if (reserved_stable_ids_.count(sid) != 0u) {
                    // Keep ghost at last (x,y) so a detection can re-match; stable_id stays claimed.
                    continue;
                }
                recycled_stable_ids_.push_back(sid);
                balls_.erase(balls_.begin() + static_cast<std::ptrdiff_t>(i));
                ball_matched.erase(ball_matched.begin() + static_cast<std::ptrdiff_t>(i));
                --i;
            }
        }
    }
    if (balls_.size() > kMaxBalls) {
        for (auto it = balls_.begin(); it != balls_.end() && balls_.size() > kMaxBalls;) {
            if (reserved_stable_ids_.count(it->stable_id) != 0u) {
                ++it;
            } else {
                recycled_stable_ids_.push_back(it->stable_id);
                it = balls_.erase(it);
            }
        }
        if (balls_.size() > kMaxBalls) {
            balls_.resize(kMaxBalls);
        }
    }
    std::sort(balls_.begin(), balls_.end(),
              [](const TrackedObject& a, const TrackedObject& b) {
                  if (a.x != b.x) return a.x < b.x;
                  return a.y < b.y;
              });

    // Putter (single best for legacy/UE)
    update_track(putter_, best_putter, dt);

    // All putters (raw per-frame, no tracking)
    putters_.clear();
    for (const Detection* d : putter_dets) {
        TrackedObject p;
        p.class_id = kPutterClassId;
        p.x = d->cx();
        p.y = d->cy();
        p.confidence = d->confidence;
        p.valid = true;
        putters_.push_back(p);
    }

    // Primary hole: sticky selection - don't switch when new hole appears. Use position-based matching.
    if (holes_.empty()) {
        frames_without_holes_++;
        if (frames_without_holes_ > kMaxFramesWithoutHoles) {
            last_primary_x_ = -1.f;
            last_primary_y_ = -1.f;
        }
        if (hole_pos_.valid) {
            hole_pos_.frames_since_seen++;
            if (hole_pos_.frames_since_seen > kHoleMaxLostFrames) hole_pos_.valid = false;
            // Freeze hole when lost: keep sending last known position so Unreal keeps it visible
            holes_.push_back(hole_pos_);
            primary_hole_index_ = 0;
        }
    } else {
        frames_without_holes_ = 0;
        size_t primary_idx = 0;
        if (target_hole_index_ >= 0 && static_cast<size_t>(target_hole_index_) < holes_.size()) {
            primary_idx = static_cast<size_t>(target_hole_index_);
        } else if (last_primary_x_ >= 0.f && last_primary_y_ >= 0.f) {
            // Sticky: pick hole closest to last selection (don't switch when new hole appears)
            float best_d2 = 1e30f;
            for (size_t i = 0; i < holes_.size(); ++i) {
                float dx = holes_[i].x - last_primary_x_;
                float dy = holes_[i].y - last_primary_y_;
                float d2 = dx * dx + dy * dy;
                if (d2 < best_d2) { best_d2 = d2; primary_idx = i; }
            }
        } else if (ball().valid) {
            float best_dist = 1e30f;
            for (size_t i = 0; i < holes_.size(); ++i) {
                float dx = ball().x - holes_[i].x;
                float dy = ball().y - holes_[i].y;
                float d2 = dx * dx + dy * dy;
                if (d2 < best_dist) { best_dist = d2; primary_idx = i; }
            }
        }
        hole_pos_ = holes_[primary_idx];
        primary_hole_index_ = static_cast<int>(primary_idx);
        last_primary_x_ = hole_pos_.x;
        last_primary_y_ = hole_pos_.y;
    }

    float ppi = (hole_pos_.radius > 0) ? (hole_pos_.radius * 2.0f / 4.25f) : 1.0f;

    // Track shortest distance to hole while primary ball is visible
    const TrackedObject& primary_ball = ball();
    if (primary_ball.valid && hole_pos_.radius > 0) {
        float dx = primary_ball.x - hole_pos_.x;
        float dy = primary_ball.y - hole_pos_.y;
        float current_dist_px = std::sqrt((dx * dx) + (dy * dy));
        if (current_dist_px < min_dist_px) {
            min_dist_px = current_dist_px;
        }
    }
    // When the primary ball disappears, check if it was close enough to the hole for putt-made.
    if (!putt_ended_fired_ && hole_pos_.radius > 0 && ball_was_valid && !primary_ball.valid) {
        putt_ended_fired_ = true;
        float ppi_local = (hole_pos_.radius > 0) ? (hole_pos_.radius * 2.0f / 4.25f) : 1.0f;
        float best_dist_px = min_dist_px;
        if (best_dist_px >= 999999.0f) {
            float dx = primary_ball.x - hole_pos_.x;
            float dy = primary_ball.y - hole_pos_.y;
            best_dist_px = std::sqrt((dx * dx) + (dy * dy));
        }
        float best_dist_inches = best_dist_px / ppi_local;
        float speed_ips = std::sqrt(primary_ball.vx * primary_ball.vx + primary_ball.vy * primary_ball.vy) / ppi_local;
        // 6.0" threshold: detection often loses ball before it reaches hole. Logs showed
        // closest approaches 3.2–4.65" for made putts; 4.0" still missed (line 50: 4.65").
        bool distance_ok = best_dist_inches < 6.0f;
        bool speed_ok = speed_ips < 45.0f;

        printf("Putt ended. Closest approach: %.2f inches (hole %s)\n",
               best_dist_inches, hole_pos_.valid ? "visible" : "last-known");

        if (distance_ok) {
            if (speed_ok) {
                this->is_putt_made = true;
                printf("RESULT: MADE IT (Closest approach was %.2f in)\n", best_dist_inches);
            }
        }

        min_dist_px = 999999.0f;
    }
}

void Tracker::update_track(TrackedObject& track, const Detection* det, double dt) {
    const bool claim_reserved = reserved_stable_ids_.count(track.stable_id) != 0u;
    if (det) {
        float new_x = det->cx();
        float new_y = det->cy();

        if (!track.valid) {
            track.x = new_x;
            track.y = new_y;
            track.vx = 0.f;
            track.vy = 0.f;
        } else {
            float prev_x = track.x;
            float prev_y = track.y;
            track.x = alpha_ * new_x + (1.f - alpha_) * track.x;
            track.y = alpha_ * new_y + (1.f - alpha_) * track.y;

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
        if (claim_reserved && !track.valid) {
            return;
        }
        track.frames_since_seen++;
        if (track.frames_since_seen > max_lost_) {
            track.valid = false;
        } else if (track.valid) {
            // Freeze position when lost (no extrapolation) so Unreal doesn't drift toward wrong target
            // track.x, track.y held at last known position
            track.vx *= 0.9f;
            track.vy *= 0.9f;
        }
    }
}

} // namespace golf
