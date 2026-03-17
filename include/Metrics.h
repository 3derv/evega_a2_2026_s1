#ifndef METRICS_H
#define METRICS_H

#include <vector>

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

} // namespace trace

#endif // METRICS_H