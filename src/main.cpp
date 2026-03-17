#include "Renderer.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>

void save_ppm(const std::string& filename, const std::vector<Vector3>& frame, int width, int height) {
    std::filesystem::create_directories(std::filesystem::path(filename).parent_path());
    std::ofstream file(filename);
    if (!file) {
        std::cerr << "Error al abrir el archivo: " << filename << std::endl;
        return;
    }
    file << "P3\n" << width << " " << height << "\n255\n";
    for (const auto& color : frame) {
        int r = static_cast<int>(std::clamp(color.x * 255, 0.0, 255.0));
        int g = static_cast<int>(std::clamp(color.y * 255, 0.0, 255.0));
        int b = static_cast<int>(std::clamp(color.z * 255, 0.0, 255.0));
        file << r << " " << g << " " << b << "\n";
    }
    file.close();
}

int main() {
    Renderer renderer;
    std::vector<Vector3> frame;

    auto start = std::chrono::high_resolution_clock::now();
    renderer.render_frame(frame);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;

    // Guardar la imagen
    save_ppm("/home/ederv/tec/p1arqui2/evega_a2_2026_s1/results/image/frame.ppm", frame, WIDTH, HEIGHT);

    // Imprimir características
    std::cout << "Características de la imagen:" << std::endl;
    std::cout << "Resolución: " << WIDTH << "x" << HEIGHT << std::endl;
    std::cout << "Número de píxeles: " << frame.size() << std::endl;
    std::cout << "Tiempo de renderizado: " << elapsed.count() << " segundos" << std::endl;
    std::cout << "Archivo guardado en: /home/ederv/tec/p1arqui2/evega_a2_2026_s1/results/image/frame.ppm" << std::endl;

    return 0;
}