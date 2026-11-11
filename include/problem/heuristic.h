#ifndef PROBLEM_HEURISTIC_H_
#define PROBLEM_HEURISTIC_H_

#include <sys/time.h>
#include <sstream>
#include <string>
#include <vector>

class Heuristic {
 public:
  Heuristic(double runtime_limit, bool validation);

  // Compute the runtime from start until now
  double Runtime();

  // Iteration limit (-i): stop after this many Report() calls (any variant), whatever the runtime; -1 = off. Read at
  // construction. Report() calls are the heuristic's own termination checks, so the count is a deterministic progress
  // index for a given seed (the runtime does not enter it).
  static long long default_iteration_limit;
  long long get_report_count() const {  return report_count_;  }

  // Getters
  double get_best() const {  return best_;  }

  /* In this section we have various functions for checking if the heuristic
   *   should keep going or stop. In all cases, the following hierarchy is used
   *   to determine whether to keep going:
   *   1) If the user provided a callback, use that callback
   *   2) Runtime-based termination criterion
   */

  // Summary of reported solutions
  std::string History();
  virtual bool IsHistoryValid() = 0;

  // Heuristic is an abstract class, so it should have a virtual destructor
  virtual ~Heuristic() {};

 protected:
  // Is this a validation run? If so we will store all solutions and evaluate
  // them after the run is complete.
  bool validation_;

  struct timeval start_time_;
  double best_;
  double runtime_limit_;
  long long iteration_limit_;
  long long report_count_;

  // Keep going while below the runtime limit and (if set) the iteration limit
  bool KeepGoing(double rt) const;

  // Information about the history of new best solutions found by the heuristic.
  std::vector<double> past_solution_values_;
  std::vector<double> past_solution_times_;
  std::vector<long long> past_solution_counts_;  // Report() calls so far at each new best

 private:
  // Disable default constructor
  Heuristic();
};

#endif
