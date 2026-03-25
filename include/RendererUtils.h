#ifndef RENDERER_UTILS_H
#define RENDERER_UTILS_H

#include "CacheModel.h"
#include "Metrics.h"
#include "Constants.h"
#include <vector>

namespace trace {

// reset_thread_stats: Reinicia el estado de contadores y caches de todos
// los threads antes de iniciar un nuevo render_frame().
//
// Extrae la lógica duplicada que existía en FinegrainedRenderer y CoarseRenderer.
// Los campos que reinicia son los mismos en ambos modelos; CoarseRenderer
// adicionalmente reinicia thread_done[] (scheduler-specific, no incluido aquí).
inline void reset_thread_stats(
    std::vector<ThreadMetrics>& stats,
    std::vector<CacheModel>&    caches)
{
    for (int i = 0; i < constants::NUM_THREADS; ++i) {
        caches[i].reset();
        stats[i].nops_count      = 0;
        stats[i].nop_time_ns     = 0.0;
        stats[i].cache_misses    = 0;
        stats[i].virtual_time_ns = 0LL;
    }
}

// sum_virtual_times: Suma el tiempo virtual de todos los threads.
//
// Extrae el bucle de acumulación duplicado en FinegrainedRenderer y CoarseRenderer.
// En ambos modelos, el VT total es la suma (no el máximo): los threads comparten
// el mismo pipeline y sus quanta se suman, no se solapan.
inline long long sum_virtual_times(const std::vector<ThreadMetrics>& stats) {
    long long total = 0LL;
    for (const auto& s : stats) total += s.virtual_time_ns;
    return total;
}

} // namespace trace

#endif // RENDERER_UTILS_H
