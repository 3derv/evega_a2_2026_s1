#include "Constants.h"
#include "GenericRunner.h"
#include "RendererFactory.h"
#include "SMTRenderer.h"
#include <iostream>
#include <string>
#include <stdexcept>

int main(int argc, char* argv[]) {
    int num_runs  = 1;
    int verbose   = 0;    // ciclos a imprimir con --verbose N (solo SMT)
    std::string model = "sequential";  // Modelo por defecto
    
    // Parsear argumentos: --model {sequential|fgmt|...} --runs N [--verbose N]
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model = argv[++i];
        } else if (arg == "--runs" && i + 1 < argc) {
            num_runs = std::stoi(argv[++i]);
        } else if (arg == "--verbose" && i + 1 < argc) {
            verbose = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << RendererFactory::get_help_message();
            return 0;
        }
    }

    try {
        // Crear renderer usando Factory Pattern
        auto renderer = RendererFactory::create(model);
        std::string model_name = renderer->get_model_name();

        // Activar verbose si se pidió y el modelo es SMT
        if (verbose > 0) {
            if (auto* smt = dynamic_cast<SMTRenderer*>(renderer.get()))
                smt->set_verbose(verbose);
            else
                std::cerr << "Advertencia: --verbose solo tiene efecto con --model smt\n";
        }
        
        // Ejecutar mediciones
        trace::GenericRunner runner(std::move(renderer), num_runs, model);
        trace::Metrics metrics = runner.run();

        // Resumen de mediciones
        std::cout << "\nResumen de Mediciones (" << model << "):" << std::endl;
        std::cout << "Número de ejecuciones: " << metrics.runs << std::endl;
        std::cout << "Tiempo total: " << metrics.total << " segundos" << std::endl;
        std::cout << "Tiempo promedio: " << metrics.avg << " segundos" << std::endl;
        std::cout << "Tiempo mínimo: " << metrics.min << " segundos" << std::endl;
        std::cout << "Tiempo máximo: " << metrics.max << " segundos" << std::endl;
        std::cout << "Desviación estándar: " << metrics.stddev << " segundos" << std::endl;
        std::cout << "Tiempo virtual (promedio): " << metrics.virtual_time_ns << " ns ("
                  << (metrics.virtual_time_ns / 1.0e6) << " ms)" << std::endl;

        // Calcular stalls promedio y CPI
        {
            int total_stalls_sum = 0;
            for (int s : metrics.stall_counts) total_stalls_sum += s;
            int avg_stalls = metrics.stall_counts.empty() ? 0
                             : total_stalls_sum / (int)metrics.stall_counts.size();
            int total_pixels = constants::IMAGE_WIDTH * constants::IMAGE_HEIGHT;
            double cpi = (metrics.virtual_time_ns > 0)
                ? (double)metrics.virtual_time_ns /
                  ((double)constants::NOP_PENALTY_NS * (double)total_pixels)
                : 0.0;
            std::cout << "Stalls promedio : " << avg_stalls << std::endl;
            std::cout << "CPI             : " << cpi << std::endl;
        }

        // Mostrar estadísticas por thread si existen
        if (!metrics.thread_metrics.empty()) {
            std::cout << "\nEstadísticas por Thread:" << std::endl;
            for (const auto& tm : metrics.thread_metrics) {
                std::cout << "  Thread " << tm.thread_id << ":" << std::endl;
                std::cout << "    Cache misses: " << tm.cache_misses << std::endl;
                std::cout << "    Tiempo virtual: " << tm.virtual_time_ns << " ns" << std::endl;
            }
        }

        // Rutas ya definidas en Exporter: no se repite la lógica modelo→archivo
        std::cout << "\nArchivo de mediciones guardado en: " << runner.get_csv_file() << std::endl;
        std::cout << "Imagen guardada en: " << runner.get_image_file() << std::endl;

        return 0;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << RendererFactory::get_help_message();
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << RendererFactory::get_help_message();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }
}
