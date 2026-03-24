#ifndef METRICS_H
#define METRICS_H

#include <vector>

namespace trace {

// ThreadMetrics: Estadísticas por hilo en modelos paralelos
struct ThreadMetrics {
    int thread_id = 0;
    long long nops_count = 0;           // Total de NOPs ejecutados
    double nop_time_ns = 0.0;           // Tiempo total en NOPs (nanosegundos)
    int cache_misses = 0;               // Total de cache misses
    long long virtual_time_ns = 0LL;    // Tiempo virtual acumulado (reloj simulado)
};

// Metrics: Estadísticas globales de ejecución
struct Metrics {
    int runs = 0;
    double total = 0.0;
    double avg = 0.0;
    double min = 0.0;
    double max = 0.0;
    double stddev = 0.0;
    std::vector<double> times;

    // Reloj virtual: tiempo simulado por el modelo arquitectónico (independiente del OS)
    long long virtual_time_ns = 0LL;       // Promedio de tiempo virtual (ns)
    std::vector<long long> virtual_times;  // Tiempo virtual por ejecución

    // Estadísticas por thread (para modelos paralelos)
    std::vector<ThreadMetrics> thread_metrics;
    std::vector<int> stall_counts;  // stalls (cache misses) por ejecución
};

} // namespace trace

#endif // METRICS_H