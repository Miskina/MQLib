#include <stdlib.h>
#include <time.h>
#include <iomanip>
#include <iostream>
#include <vector>
#include "problem/heuristic.h"

Heuristic::Heuristic(double runtime_limit, bool validation) :
  validation_(validation),
  best_(0.0),
  runtime_limit_(runtime_limit),
  iteration_limit_(default_iteration_limit),
  report_count_(0) {
  gettimeofday(&start_time_, 0);
}

long long Heuristic::default_iteration_limit = -1;

bool Heuristic::KeepGoing(double rt) const {
  if (iteration_limit_ >= 0 && report_count_ >= iteration_limit_) {
    return false;
  }
  return rt < runtime_limit_;
}

double Heuristic::Runtime() {
  struct timeval tv;
  gettimeofday(&tv, 0);
  double secs = (tv.tv_sec - start_time_.tv_sec) + 0.000001 *
    (tv.tv_usec - start_time_.tv_usec);
  return secs;
}

std::string Heuristic::History() {
  std::stringstream out_str;
  out_str << std::setprecision(15) << "[";
  for (int i = 0; i < past_solution_values_.size(); i++) {
    if (i > 0) out_str << ";";
    out_str << past_solution_values_[i] << ":" << past_solution_times_[i] << ":" <<
      past_solution_counts_[i];
  }
  out_str << "]";
  return out_str.str();
}
