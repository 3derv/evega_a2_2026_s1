#include "FinegrainedRunner.h"
#include "Constants.h"
#include <numeric>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace constants;
using namespace trace;

FinegrainedRunner::FinegrainedRunner(int runs, const std::string& model) 
    : runs_(std::max(runs, 1)), model_(model), exporter_(model) {}

double FinegrainedRunner::run_once(std::vector<ThreadMetrics>& thread_metrics) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Ejecutar renderizado FGMT (4 threads)
    std::vector<Vector3> frame = renderer_.render_frame();
    
    // Obtener métricas de threads
    thread_metrics = renderer_.get_thread_metrics();
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    
    return elapsed.count();
}

Metrics FinegrainedRunner::run() {
    Metrics metrics;
    metrics.runs = runs_;
    metrics.times.reserve(runs_);
    
    // Inicializar agregado de estadísticas por thread
    std::vector<long long> nops_by_thread(constants::NUM_THREADS, 0);
    std::vector<double> nop_time_by_thread(constants::NUM_THREADS, 0.0);
    std::vector<int> misses_by_thread(constants::NUM_THREADS, 0);
    
    // Ejecutar N veces
    for (int i = 0; i < runs_; ++i) {
        std::vector<ThreadMetrics> thread_stats;
        double elapsed = run_once(thread_stats);
        metrics.times.push_back(elapsed);
        
        // Acumular estadísticas de threads (para promedio al final)
        for (const auto& ts : thread_stats) {
            nops_by_thread[ts.thread_id] += ts.nops_count;
            nop_time_by_thread[ts.thread_id] += ts.nop_time_ns;
            misses_by_thread[ts.thread_id] += ts.cache_misses;
        }
    }
    
    // Calcular estadísticas globales
    metrics.total = std::accumulate(metrics.times.begin(), metrics.times.end(), 0.0);
    metrics.avg = metrics.total / metrics.runs;
    metrics.min = *std::min_element(metrics.times.begin(), metrics.times.end());
    metrics.max = *std::max_element(metrics.times.begin(), metrics.times.end());
    
    // Calcular desviación estándar
    double sum_sq_diff = 0.0;
    for (double t : metrics.times) {
        sum_sq_diff += (t - metrics.avg) * (t - metrics.avg);
    }
    metrics.stddev = std::sqrt(sum_sq_diff / metrics.runs);
    
    // Preparar ThreadMetrics agregadas para export
    metrics.thread_metrics.resize(constants::NUM_THREADS);
    for (int i = 0; i < constants::NUM_THREADS; ++i) {
        metrics.thread_metrics[i].thread_id = i;
        metrics.thread_metrics[i].nops_count = nops_by_thread[i] / runs_;  // Promedio
        metrics.thread_metrics[i].nop_time_ns = nop_time_by_thread[i] / runs_;
        metrics.thread_metrics[i].cache_misses = misses_by_thread[i] / runs_;
    }
    
    // Exportar resultados
    exporter_.save_image(renderer_.get_frame());
    exporter_.save_csv(metrics);
    
    return metrics;
}
