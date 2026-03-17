#include "Runner.h"
#include "Constants.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace trace {

Runner::Runner(int runs) : runs_(runs) {
    if (runs_ < 1) {
        runs_ = 1;
    }
}

double Runner::run_once(std::vector<Vector3>& frame) {
    auto start = std::chrono::high_resolution_clock::now();
    renderer_.render_frame(frame);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    return elapsed.count();
}

Metrics Runner::run() {
    Metrics result;
    result.runs = runs_;
    std::vector<Vector3> frame;
    frame.reserve(constants::IMAGE_WIDTH * constants::IMAGE_HEIGHT);

    for (int i = 0; i < runs_; ++i) {
        double time = run_once(frame);
        result.times.push_back(time);
    }

    result.total = std::accumulate(result.times.begin(), result.times.end(), 0.0);
    result.avg = result.total / runs_;
    result.min = *std::min_element(result.times.begin(), result.times.end());
    result.max = *std::max_element(result.times.begin(), result.times.end());

    double variance = 0.0;
    for (double t : result.times) {
        variance += (t - result.avg) * (t - result.avg);
    }
    variance /= runs_;
    result.stddev = std::sqrt(variance);

    exporter_.save_image(frame);
    exporter_.save_csv(result);

    return result;
}

} // namespace trace
