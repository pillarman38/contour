// ─────────────────────────────────────────────────────────────────────────────
// putt_stats.cpp  –  Putting Statistics State Machine
// ─────────────────────────────────────────────────────────────────────────────

#include "putt_stats.h"

#include <cmath>
#include <numeric>

namespace golf {

PuttStats::PuttStats(float motion_threshold, int stop_frames, float max_motion_sec)
    : motion_threshold_(motion_threshold),
      stop_frames_required_(stop_frames),
      max_motion_sec_(max_motion_sec) {}

void PuttStats::update(const TrackedObject& ball, double dt, float ppi) {
    std::lock_guard<std::mutex> lock(mu_);

    if (!ball.valid) {
        has_prev_ = false;
        return;
    }

    float speed_ips = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy) / ppi;
    current_.current_speed = speed_ips;

    // Accumulate distance from frame-to-frame movement
    if (has_prev_) {
        float dx = ball.x - prev_x_;
        float dy = ball.y - prev_y_;
        // Convert frame distance to inches
        float frame_dist_inches = std::sqrt(dx * dx + dy * dy) / ppi;

        if (current_.state == PuttState::IN_MOTION) {
            current_.total_distance += frame_dist_inches;
            current_.time_in_motion += static_cast<float>(dt);

            if (speed_ips > current_.peak_speed) {
                current_.peak_speed = speed_ips;
            }

            frame_samples_.push_back({ speed_ips, current_.total_distance, current_.time_in_motion, ball.x, ball.y });

            if (has_direction_) {
                float rx = ball.x - current_.start_x;
                float ry = ball.y - current_.start_y;
                // Convert perpendicular break to inches
                float cross_px = std::abs(rx * dir_y_ - ry * dir_x_);
                float cross_inches = cross_px / ppi;
                
                if (cross_inches > current_.break_distance) {
                    current_.break_distance = cross_inches;
                }
            }

            current_.final_x = ball.x;
            current_.final_y = ball.y;
        }
    }

    prev_x_ = ball.x;
    prev_y_ = ball.y;
    has_prev_ = true;

    // State transitions (Using speed_ips for thresholding)
    switch (current_.state) {
        case PuttState::IDLE:
        case PuttState::STOPPED:
            if (speed_ips > motion_threshold_) {
                current_.state = PuttState::IN_MOTION;
                current_.putt_number = static_cast<int>(history_.size()) + 1;
                current_.launch_speed = speed_ips;
                current_.peak_speed = speed_ips;
                current_.total_distance = 0.f;
                current_.break_distance = 0.f;
                current_.time_in_motion = 0.f;
                current_.start_x = ball.x;
                current_.start_y = ball.y;
                frame_samples_.clear();
                frame_samples_.push_back({ speed_ips, 0.f, 0.f, ball.x, ball.y });
            }
            break;

        case PuttState::IN_MOTION:
            if (current_.time_in_motion >= max_motion_sec_) {
                current_.state = PuttState::STOPPED;
                finalize_putt();
            } else if (speed_ips < motion_threshold_) {
                frames_below_threshold_++;
                if (frames_below_threshold_ >= stop_frames_required_) {
                    current_.state = PuttState::STOPPED;
                    finalize_putt();
                }
            } else {
                frames_below_threshold_ = 0;
            }
            break;
    }
}

void PuttStats::on_ball_lost() {
    std::lock_guard<std::mutex> lock(mu_);
    if (current_.state == PuttState::IN_MOTION) {
        current_.state = PuttState::STOPPED;
        finalize_putt();
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
    if (!frame_samples_.empty()) {
        current_.launch_speed = frame_samples_.front().speed_ips;
        current_.total_distance = frame_samples_.back().cumulative_distance;
        current_.time_in_motion = frame_samples_.back().time_in_motion;
        float peak = 0.f;
        for (const auto& s : frame_samples_) {
            if (s.speed_ips > peak) {
                peak = s.speed_ips;
                current_.peak_speed_x = s.x;
                current_.peak_speed_y = s.y;
            }
        }
        current_.peak_speed = peak;
    }
    history_.push_back(current_);
}

}  // namespace golf
