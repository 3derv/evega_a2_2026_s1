#include "Exporter.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace trace {

Exporter::Exporter(const std::string& model) : model_(model) {
    // Configurar rutas basadas en el modelo
    if (model == "fgmt") {
        image_file_ = constants::IMAGE_FILE_FGMT;
        csv_file_   = constants::CSV_FILE_FGMT;
    } else if (model == "cgmt") {
        image_file_ = constants::IMAGE_FILE_CGMT;
        csv_file_   = constants::CSV_FILE_CGMT;
    } else if (model == "smt") {
        image_file_ = constants::IMAGE_FILE_SMT;
        csv_file_   = constants::CSV_FILE_SMT;
    } else if (model == "cmp") {
        image_file_ = constants::IMAGE_FILE_CMP;
        csv_file_   = constants::CSV_FILE_CMP;
    } else {
        // Defecto: secuencial
        image_file_ = constants::IMAGE_FILE_SEQUENTIAL;
        csv_file_   = constants::CSV_FILE_SEQUENTIAL;
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
    file << "Ejecucion,Tiempo(s),TiempoVirtual(ns),Stalls\n";
    for (size_t i = 0; i < metrics.times.size(); ++i) {
        long long vt     = (i < metrics.virtual_times.size()) ? metrics.virtual_times[i] : 0LL;
        int       stalls = (i < metrics.stall_counts.size())  ? metrics.stall_counts[i]  : 0;
        file << (i + 1) << "," << metrics.times[i] << "," << vt << "," << stalls << "\n";
    }
}

} // namespace trace
