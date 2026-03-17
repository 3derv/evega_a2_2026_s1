#include "Constants.h"
#include "Runner.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    int num_runs = 1; // Por defecto, una ejecución
    if (argc > 1 && std::string(argv[1]) == "--runs") {
        if (argc > 2) {
            num_runs = std::stoi(argv[2]);
        }
    }

    trace::Runner runner(num_runs);
    trace::Metrics metrics = runner.run();

    // Resumen de mediciones
    std::cout << "\nResumen de Mediciones (Secuencial):" << std::endl;
    std::cout << "Número de ejecuciones: " << metrics.runs << std::endl;
    std::cout << "Tiempo total: " << metrics.total << " segundos" << std::endl;
    std::cout << "Tiempo promedio: " << metrics.avg << " segundos" << std::endl;
    std::cout << "Tiempo mínimo: " << metrics.min << " segundos" << std::endl;
    std::cout << "Tiempo máximo: " << metrics.max << " segundos" << std::endl;
    std::cout << "Desviación estándar: " << metrics.stddev << " segundos" << std::endl;
    std::cout << "Speedup (respecto a secuencial): 1.0" << std::endl;
    std::cout << "Eficiencia paralela: 1.0 (1 hilo)" << std::endl;
    std::cout << "Escalabilidad: N/A (1 hilo)" << std::endl;
    std::cout << "Archivo de mediciones guardado en: " << constants::CSV_FILE << std::endl;
    std::cout << "Imagen guardada en: " << constants::IMAGE_FILE << std::endl;

    return 0;
}