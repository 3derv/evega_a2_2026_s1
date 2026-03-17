#include "Constants.h"
#include "Renderer.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <string>
#include <numeric>

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

double run_single_render(Renderer& renderer, std::vector<Vector3>& frame) {
    auto start = std::chrono::high_resolution_clock::now();
    renderer.render_frame(frame);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    return elapsed.count();
}

int main(int argc, char* argv[]) {
    int num_runs = 1; // Por defecto, una ejecución
    if (argc > 1 && std::string(argv[1]) == "--runs") {
        if (argc > 2) {
            num_runs = std::stoi(argv[2]);
        }
    }

    Renderer renderer;
    std::vector<Vector3> frame;
    std::vector<double> times;

    // Ejecutar múltiples veces para mediciones
    for (int i = 0; i < num_runs; ++i) {
        double time = run_single_render(renderer, frame);
        times.push_back(time);
        std::cout << "Ejecución " << (i + 1) << ": " << time << " segundos" << std::endl;
    }

    // Calcular métricas
    double total_time = std::accumulate(times.begin(), times.end(), 0.0);
    double avg_time = total_time / num_runs;
    double min_time = *std::min_element(times.begin(), times.end());
    double max_time = *std::max_element(times.begin(), times.end());

    // Calcular desviación estándar
    double variance = 0.0;
    for (double t : times) {
        variance += (t - avg_time) * (t - avg_time);
    }
    variance /= num_runs;
    double std_dev = std::sqrt(variance);

    // Guardar imagen de la última ejecución
    save_ppm(constants::IMAGE_FILE, frame, constants::IMAGE_WIDTH, constants::IMAGE_HEIGHT);

    // Guardar resultados de mediciones en CSV
    std::filesystem::create_directories(constants::RESULTS_DIR);
    std::ofstream csv_file(constants::CSV_FILE);
    csv_file << "Ejecucion,Tiempo(s)\n";
    for (size_t i = 0; i < times.size(); ++i) {
        csv_file << (i + 1) << "," << times[i] << "\n";
    }
    csv_file.close();

    // Imprimir resumen
    std::cout << "\nResumen de Mediciones (Secuencial):" << std::endl;
    std::cout << "Número de ejecuciones: " << num_runs << std::endl;
    std::cout << "Tiempo total: " << total_time << " segundos" << std::endl;
    std::cout << "Tiempo promedio: " << avg_time << " segundos" << std::endl;
    std::cout << "Tiempo mínimo: " << min_time << " segundos" << std::endl;
    std::cout << "Tiempo máximo: " << max_time << " segundos" << std::endl;
    std::cout << "Desviación estándar: " << std_dev << " segundos" << std::endl;
    std::cout << "Speedup (respecto a secuencial): 1.0" << std::endl;
    std::cout << "Eficiencia paralela: 1.0 (1 hilo)" << std::endl;
    std::cout << "Escalabilidad: N/A (1 hilo)" << std::endl;
    std::cout << "Archivo de mediciones guardado en: " << constants::CSV_FILE << std::endl;
    std::cout << "Imagen guardada en: " << constants::IMAGE_FILE << std::endl;

    return 0;
}