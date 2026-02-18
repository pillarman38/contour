// ─────────────────────────────────────────────────────────────────────────────
// putt_stats.cpp  –  Putting Statistics State Machine
// ─────────────────────────────────────────────────────────────────────────────

#include "putt_stats.h"

#include <cmath>
#include <numeric>

namespace golf {

PuttStats::PuttStats(float motion_threshold, int stop_frames)
    : motion_threshold_(motion_threshold),
      stop_frames_required_(stop_frames) {}

void PuttStats::update(const TrackedObject& ball, double dt) {
    std::lock_guard<std::mutex> lock(mu_);

    if (!ball.valid) {
        has_prev_ = false;
        return;
    }

    float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
    current_.current_speed = speed;

    // Accumulate distance from frame-to-frame movement
    if (has_prev_) {
        float dx = ball.x - prev_x_;
        float dy = ball.y - prev_y_;
        float frame_dist = std::sqrt(dx * dx + dy * dy);

        if (current_.state == PuttState::IN_MOTION) {
            current_.total_distance += frame_dist;
            current_.time_in_motion += static_cast<float>(dt);

            if (speed > current_.peak_speed) {
                current_.peak_speed = speed;
            }

            // Compute break: perpendicular distance from the initial putt line
            if (has_direction_) {
                float rx = ball.x - current_.start_x;
                float ry = ball.y - current_.start_y;
                // Cross product gives signed perpendicular distance
                float cross = std::abs(rx * dir_y_ - ry * dir_x_);
                if (cross > current_.break_distance) {
                    current_.break_distance = cross;
                }
            }

            current_.final_x = ball.x;
            current_.final_y = ball.y;
        }
    }

    prev_x_ = ball.x;
    prev_y_ = ball.y;
    has_prev_ = true;

    // State transitions
    switch (current_.state) {
        case PuttState::IDLE:
            if (speed > motion_threshold_) {
                current_.state = PuttState::IN_MOTION;
                current_.putt_number = static_cast<int>(history_.size()) + 1;
                current_.launch_speed = speed;
                current_.peak_speed = speed;
                current_.total_distance = 0.f;
                current_.break_distance = 0.f;
                current_.time_in_motion = 0.f;
                current_.start_x = ball.x;
                current_.start_y = ball.y;
                current_.final_x = ball.x;
                current_.final_y = ball.y;

                // Capture initial direction for break computation
                float vmag = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
                if (vmag > 1e-6f) {
                    dir_x_ = ball.vx / vmag;
                    dir_y_ = ball.vy / vmag;
                    has_direction_ = true;
                } else {
                    has_direction_ = false;
                }
                frames_below_threshold_ = 0;
            }
            break;

        case PuttState::IN_MOTION:
            if (speed < motion_threshold_) {
                frames_below_threshold_++;
                if (frames_below_threshold_ >= stop_frames_required_) {
                    current_.state = PuttState::STOPPED;
                    finalize_putt();
                }
            } else {
                frames_below_threshold_ = 0;
            }
            break;

        case PuttState::STOPPED:
            if (speed > motion_threshold_) {
                // New putt begins
                current_.state = PuttState::IN_MOTION;
                current_.putt_number = static_cast<int>(history_.size()) + 1;
                current_.launch_speed = speed;
                current_.peak_speed = speed;
                current_.total_distance = 0.f;
                current_.break_distance = 0.f;
                current_.time_in_motion = 0.f;
                current_.start_x = ball.x;
                current_.start_y = ball.y;
                current_.final_x = ball.x;
                current_.final_y = ball.y;

                float vmag = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
                if (vmag > 1e-6f) {
                    dir_x_ = ball.vx / vmag;
                    dir_y_ = ball.vy / vmag;
                    has_direction_ = true;
                } else {
                    has_direction_ = false;
                }
                frames_below_threshold_ = 0;
            }
            break;
    }
}

PuttData PuttStats::current() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_;
}

std::vector<PuttData> PuttStats::history() const {
    std::lock_guard<std::mutex> lock(mu_);
    return history_;
}

PuttStats::SessionSummary PuttStats::session() const {
    std::lock_guard<std::mutex> lock(mu_);
    SessionSummary s;
    s.total_putts = static_cast<int>(history_.size());
    if (s.total_putts == 0) return s;

    for (const auto& p : history_) {
        s.avg_launch_speed += p.launch_speed;
        s.avg_distance += p.total_distance;
        s.avg_break += p.break_distance;
        s.avg_time += p.time_in_motion;
    }
    float n = static_cast<float>(s.total_putts);
    s.avg_launch_speed /= n;
    s.avg_distance /= n;
    s.avg_break /= n;
    s.avg_time /= n;
    return s;
}

void PuttStats::finalize_putt() {
    history_.push_back(current_);
}

}  // namespace golf
