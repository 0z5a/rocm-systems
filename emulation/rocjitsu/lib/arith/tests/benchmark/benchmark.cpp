// Copyright (c) 2026 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

// Benchmark command-line parsing, workload ordering, and CSV output.

#include "benchmark/csv.h"
#include "benchmark/ordering.h"
#include "benchmark/workloads.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <system_error>

namespace {

// Parse a strictly positive decimal integer; return false for invalid or trailing text.
bool parse_positive_integer(const char *text, int &value) {
  const char *end = text + std::strlen(text);
  auto parsed = std::from_chars(text, end, value);
  return parsed.ec == std::errc{} && parsed.ptr == end && value > 0;
}

// Parse a finite nonnegative duration; return false for invalid or trailing text.
bool parse_duration(const char *text, double &value) {
  char *end;
  errno = 0;
  value = std::strtod(text, &end);
  return *text && end != text && !*end && !errno && std::isfinite(value) && value >= 0;
}

// Parse benchmark option/value pairs; reject unknown, repeated, or incomplete options.
bool parse_options(int argc, char **argv, rocjitsu::arith::test::Options &options) {
  std::set<std::string> seen;
  for (int arg = 1; arg < argc; arg += 2) {
    std::string name = argv[arg];
    if (arg + 1 == argc || !seen.insert(name).second) {
      return false;
    }
    const char *value = argv[arg + 1];
    if (name == "--min-ms") {
      if (!parse_duration(value, options.min_ms)) {
        return false;
      }
    } else if (name == "--repetitions") {
      if (!parse_positive_integer(value, options.repetitions)) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

} // namespace

// Run the requested benchmark repetitions and emit CSV; report invalid options or failures on
// stderr.
int main(int argc, char **argv) {
  rocjitsu::arith::test::Options options;
  if (!parse_options(argc, argv, options)) {
    std::fprintf(stderr,
                 "Usage: %s [--min-ms N] [--repetitions N]\n"
                 "Defaults: 1 ms per repetition, 5 repetitions.\n",
                 argv[0]);
    return 2;
  }
  try {
    std::vector<rocjitsu::arith::test::Benchmark> cases;
    rocjitsu::arith::test::add_wmma(cases);
    rocjitsu::arith::test::add_dot(cases);
    rocjitsu::arith::test::add_fp(cases);
    rocjitsu::arith::test::add_math(cases);
    std::stable_sort(cases.begin(), cases.end(), rocjitsu::arith::test::benchmark_less);
    rocjitsu::arith::test::run(cases, options, stderr);
    rocjitsu::arith::test::print_csv(stdout, cases);
    if (std::fflush(stdout) != 0 || std::ferror(stdout)) {
      throw std::runtime_error("Failed to write benchmark CSV");
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "Benchmark failed: %s\n", error.what());
    return 1;
  }
}
