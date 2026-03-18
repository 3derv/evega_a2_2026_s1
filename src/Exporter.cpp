#include "Exporter.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace trace {

Exporter::Exporter(const std::string& model) : model_(model) {
    // Configurar rutas basadas en el modelo
    if (model == "fgmt") {
        image_file_ = constants::RESULTS_DIR + "/image/frame_fgmt.ppm";
        csv_file_ = constants::RESULTS_DIR + "/mediciones_fgmt.csv";
    } else if (model == "cgmt") {
        image_file_ = constants::RESULTS_DIR + "/image/frame_cgmt.ppm";
        csv_file_ = constants::RESULTS_DIR + "/mediciones_cgmt.csv";
    } else if (model == "smt") {
        image_file_ = constants::RESULTS_DIR + "/image/frame_smt.ppm";
        csv_file_ = constants::RESULTS_DIR + "/mediciones_smt.csv";
    } else if (model == "cmp") {
        image_file_ = constants::RESULTS_DIR + "/image/frame_cmp.ppm";
        csv_file_ = constants::RESULTS_DIR + "/mediciones_cmp.csv";
    } else {
        // Defecto: secuencial
        image_file_ = constants::IMAGE_FILE;  // results/image/frame.ppm
        csv_file_ = constants::CSV_FILE;      // results/mediciones_secuencial.csv
    }
}

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
    std::ofstream file(csv_file_);
    if (!file) {
        return;
    }
    file << "Ejecucion,Tiempo(s)\n";
    for (size_t i = 0; i < metrics.times.size(); ++i) {
        file << (i + 1) << "," << metrics.times[i] << "\n";
    }
}

} // namespace trace
