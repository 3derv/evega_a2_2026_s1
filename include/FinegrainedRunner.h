#ifndef FINEGRAINED_RUNNER_H
#define FINEGRAINED_RUNNER_H

#include "FinegrainedRenderer.h"
#include "Exporter.h"
#include "Metrics.h"
#include <vector>
#include <chrono>

namespace trace {

// FinegrainedRunner: Ejecutor de mediciones para modelo FGMT (Fine-Grained Multithreading).
// 
// Responsabilidad:
//   - Ejecutar FinegrainedRenderer N veces para recopilar mediciones de tiempo.
//   - Calcular estadísticas (promedio, min, max, desviación estándar).
//   - Recolectar estadísticas por thread (NOPs, cache misses).
//   - Delegar al Exporter para guardar resultados (imagen y métricas CSV).
// 
// Diferencias vs Runner (secuencial):
//   - Soporta múltiples threads (4 en este caso, procesando tiles).
//   - Cada thread cuenta sus propios NOPs ejecutados durante stalls.
//   - Métricas extendidas: nops_count, nop_time_ns, cache_misses por thread.
// 
// Notas:
//   - El quantum es procesar 1 píxel (ray-sphere intersection).
//   - Los stalls se simulan ejecutando NOPs cuando hay cache miss.
//   - La localidad espacial se aprovecha dividiendo frame en 4 tiles.
struct FinegrainedRunner {
    // Constructor.
    // Param: runs - Número de ejecuciones a realizar (mínimo 1).
    //        model - Nombre del modelo (usado por Exporter para nombres de archivos).
    explicit FinegrainedRunner(int runs = 1, const std::string& model = "fgmt");

    // Ejecuta el renderizado FGMT N veces y retorna métricas agregadas.
    // Return: Metrics con tiempos globales y estadísticas por thread.
    Metrics run();

private:
    int runs_;                      // Número de ejecuciones a realizar
    std::string model_;             // Nombre del modelo
    FinegrainedRenderer renderer_;  // Instancia del renderer FGMT
    Exporter exporter_;             // Instancia para guardar resultados

    // Ejecuta una única iteración de renderizado FGMT y mide su tiempo.
    // Param: thread_metrics - Vector donde almacenar stats por thread.
    // Return: Tiempo de ejecución en segundos.
    double run_once(std::vector<ThreadMetrics>& thread_metrics);
};

} // namespace trace

#endif // FINEGRAINED_RUNNER_H
