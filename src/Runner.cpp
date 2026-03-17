#include "Runner.h"
#include "Constants.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <algorithm>
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

void Runner::save_image(const std::vector<Vector3>& frame) const {
    std::filesystem::create_directories(std::filesystem::path(constants::IMAGE_FILE).parent_path());
    std::ofstream file(constants::IMAGE_FILE);
    if (!file) {
        return;
    }
    file << "P3\n" << constants::IMAGE_WIDTH << " " << constants::IMAGE_HEIGHT << "\n255\n";
    for (const auto& color : frame) {
        int r = static_cast<int>(std::clamp(color.x * 255, 0.0, 255.0));
        int g = static_cast<int>(std::clamp(color.y * 255, 0.0, 255.0));
        int b = static_cast<int>(std::clamp(color.z * 255, 0.0, 255.0));
        file << r << " " << g << " " << b << "\n";
    }
}

void Runner::save_csv(const Metrics& metrics) const {
    std::filesystem::create_directories(constants::RESULTS_DIR);
    std::ofstream csv_file(constants::CSV_FILE);
    if (!csv_file) {
        return;
    }
    csv_file << "Ejecucion,Tiempo(s)\n";
    for (size_t i = 0; i < metrics.times.size(); ++i) {
        csv_file << (i + 1) << "," << metrics.times[i] << "\n";
    }
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

    save_image(frame);
    save_csv(result);

    return result;
}

} // namespace trace
