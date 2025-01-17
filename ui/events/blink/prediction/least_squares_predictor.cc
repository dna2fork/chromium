// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/blink/prediction/least_squares_predictor.h"

#include <cmath>

namespace ui {

namespace {

constexpr double kEpsilon = std::numeric_limits<double>::epsilon();

// Solve XB = y.
static bool SolveLeastSquares(const gfx::Matrix3F& x,
                              const std::deque<double>& y,
                              gfx::Vector3dF& result) {
  // return last point if y didn't change.
  if (std::abs(y[0] - y[1]) < kEpsilon && std::abs(y[1] - y[2]) < kEpsilon) {
    result = gfx::Vector3dF(y[2], 0, 0);
    return true;
  }

  gfx::Matrix3F x_transpose = x.Transpose();
  gfx::Matrix3F temp = gfx::MatrixProduct(x_transpose, x).Inverse();

  // Return false if x is singular.
  if (temp == gfx::Matrix3F::Zeros())
    return false;

  result = gfx::MatrixProduct(gfx::MatrixProduct(temp, x_transpose),
                              gfx::Vector3dF(y[0], y[1], y[2]));
  return true;
}

}  // namespace

LeastSquaresPredictor::LeastSquaresPredictor() {}

LeastSquaresPredictor::~LeastSquaresPredictor() {}

void LeastSquaresPredictor::Reset() {
  x_queue_.clear();
  y_queue_.clear();
  time_.clear();
}

void LeastSquaresPredictor::Update(const InputData& cur_input) {
  // Reset curve if last point is 50 milliseconds away.
  constexpr double max_interval_millisecond = 50.0;
  if (!time_.empty() &&
      (cur_input.time_stamp - time_.back()).InMillisecondsF() >
          max_interval_millisecond)
    Reset();

  x_queue_.push_back(cur_input.pos_x);
  y_queue_.push_back(cur_input.pos_y);
  time_.push_back(cur_input.time_stamp);
  if (time_.size() > kSize) {
    x_queue_.pop_front();
    y_queue_.pop_front();
    time_.pop_front();
  }
}

bool LeastSquaresPredictor::HasPrediction() const {
  return time_.size() >= kSize;
}

gfx::Matrix3F LeastSquaresPredictor::GetXMatrix() const {
  gfx::Matrix3F x = gfx::Matrix3F::Zeros();
  double t1 = (time_[1] - time_[0]).InMillisecondsF();
  double t2 = (time_[2] - time_[0]).InMillisecondsF();
  x.set(1, 0, 0, 1, t1, t1 * t1, 1, t2, t2 * t2);
  return x;
}

bool LeastSquaresPredictor::GeneratePrediction(base::TimeTicks frame_time,
                                               InputData* result) const {
  if (!HasPrediction())
    return false;

  gfx::Matrix3F time_matrix = GetXMatrix();

  double dt = (frame_time - time_[0]).InMillisecondsF();
  if (dt > 0) {
    gfx::Vector3dF b1, b2;
    if (SolveLeastSquares(time_matrix, x_queue_, b1) &&
        SolveLeastSquares(time_matrix, y_queue_, b2)) {
      gfx::Vector3dF prediction_time(1, dt, dt * dt);

      result->pos_x = gfx::DotProduct(prediction_time, b1);
      result->pos_y = gfx::DotProduct(prediction_time, b2);
      return true;
    }
  }
  return false;
}

}  // namespace ui
