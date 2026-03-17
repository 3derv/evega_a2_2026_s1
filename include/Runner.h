#ifndef RUNNER_H
#define RUNNER_H

#include "Constants.h"
#include "Renderer.h"
#include <vector>
#include <string>

namespace trace {

struct Metrics {
    int runs = 0;
    double total = 0.0;
    double avg = 0.0;
    double min = 0.0;
    double max = 0.0;
    double stddev = 0.0;
    std::vector<double> times;
};

struct Runner {
    explicit Runner(int runs = 1);

    Metrics run();

private:
    int runs_;
    Renderer renderer_;

    double run_once(std::vector<Vector3>& frame);
    void save_image(const std::vector<Vector3>& frame) const;
    void save_csv(const Metrics& metrics) const;
};

} // namespace trace

#endif // RUNNER_H