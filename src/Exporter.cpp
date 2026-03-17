#include "Exporter.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace trace {

void Exporter::save_image(const std::vector<Vector3>& frame) const {
    std::filesystem::create_directories(std::filesystem::path(image_file_).parent_path());
    std::ofstream file(image_file_);
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

void Exporter::save_csv(const Metrics& metrics) const {
    std::filesystem::create_directories(constants::RESULTS_DIR);
    std::ofstream csv_file(csv_file_);
    if (!csv_file) {
        return;
    }
    csv_file << "Ejecucion,Tiempo(s)\n";
    for (size_t i = 0; i < metrics.times.size(); ++i) {
        csv_file << (i + 1) << "," << metrics.times[i] << "\n";
    }
}

} // namespace trace
