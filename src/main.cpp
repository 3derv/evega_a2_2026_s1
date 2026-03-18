#include "Constants.h"
#include "Runner.h"
#include "FinegrainedRunner.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    int num_runs = 1;
    std::string model = "sequential";  // Modelo por defecto
    
    // Parsear argumentos: --model {sequential|fgmt} --runs N
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model = argv[++i];
        } else if (arg == "--runs" && i + 1 < argc) {
            num_runs = std::stoi(argv[++i]);
        }
    }

    trace::Metrics metrics;
    std::string model_name;
    
    // Seleccionar renderer basado en modelo
    if (model == "fgmt") {
        model_name = "Fine-Grained Multithreading (FGMT)";
        trace::FinegrainedRunner runner(num_runs, "fgmt");
        metrics = runner.run();
    } else {
        // Por defecto: sequential
        model_name = "Secuencial";
        trace::Runner runner(num_runs, "sequential");
        metrics = runner.run();
    }

    // Resumen de mediciones
    std::cout << "\nResumen de Mediciones (" << model_name << "):" << std::endl;
    std::cout << "Número de ejecuciones: " << metrics.runs << std::endl;
    std::cout << "Tiempo total: " << metrics.total << " segundos" << std::endl;
    std::cout << "Tiempo promedio: " << metrics.avg << " segundos" << std::endl;
    std::cout << "Tiempo mínimo: " << metrics.min << " segundos" << std::endl;
    std::cout << "Tiempo máximo: " << metrics.max << " segundos" << std::endl;
    std::cout << "Desviación estándar: " << metrics.stddev << " segundos" << std::endl;
    
    // Mostrar estadísticas por thread si existen
    if (!metrics.thread_metrics.empty()) {
        std::cout << "\nEstadísticas por Thread:" << std::endl;
        for (const auto& tm : metrics.thread_metrics) {
            std::cout << "  Thread " << tm.thread_id << ":" << std::endl;
            std::cout << "    NOPs ejecutados: " << tm.nops_count << std::endl;
            std::cout << "    Tiempo en NOPs: " << tm.nop_time_ns << " ns" << std::endl;
            std::cout << "    Cache misses: " << tm.cache_misses << std::endl;
        }
    }
    
    // Determinar nombres de archivos según el modelo
    std::string csv_file, img_file;
    if (model == "fgmt") {
        csv_file = "results/mediciones_fgmt.csv";
        img_file = "results/image/frame_fgmt.ppm";
    } else if (model == "cgmt") {
        csv_file = "results/mediciones_cgmt.csv";
        img_file = "results/image/frame_cgmt.ppm";
    } else if (model == "smt") {
        csv_file = "results/mediciones_smt.csv";
        img_file = "results/image/frame_smt.ppm";
    } else if (model == "cmp") {
        csv_file = "results/mediciones_cmp.csv";
        img_file = "results/image/frame_cmp.ppm";
    } else {
        csv_file = constants::CSV_FILE;
        img_file = constants::IMAGE_FILE;
    }
    
    std::cout << "\nArchivo de mediciones guardado en: " << csv_file << std::endl;
    std::cout << "Imagen guardada en: " << img_file << std::endl;

    return 0;
}
