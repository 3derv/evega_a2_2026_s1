#ifndef COARSE_RENDERER_H
#define COARSE_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// CoarseRenderer: Renderizador CGMT (Coarse-Grained Multithreading) con Scheduler Formal.
//
// Modelo CGMT simplificado pero correcto:
//   - Solo un thread ejecuta a la vez (sincronización vía condition_variable)
//   - Cada thread procesa su bloque secuencialmente
//   - En cache miss (stall): cede CPU al siguiente thread (round-robin)
//   - Sin deadlocks: sincronización clara de entrada/salida de secciones críticas
//
class CoarseRenderer : public IRenderer {
private:
    Scene scene;
    std::vector<Vector3> frame;
    std::vector<CacheModel> cache_models;
    std::vector<trace::ThreadMetrics> thread_stats;
    
    struct Task {
        int start, end;
        int thread_id;
    };
    std::vector<Task> tasks;
    
    // Variables de scheduler
    std::mutex sched_mutex;
    std::condition_variable sched_cv;
    int current_thread;                    // Cuál thread tiene control
    std::vector<bool> thread_done;         // Marca si thread completó su tarea
    std::atomic<int> threads_finished;     // Contador de threads terminados
    
    long long virtual_time_ns_ = 0LL; // Tiempo virtual del último render_frame() = suma(threads)
    void render_worker(int thread_id);

public:
    CoarseRenderer();
    std::vector<Vector3> render_frame() override;
    std::string get_model_name() const override { return "cgmt"; }
    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const { 
        return thread_stats; 
    }
    long long get_virtual_time_ns() const override { return virtual_time_ns_; }
    int get_total_stalls() const override {
        int total = 0;
        for (const auto& ts : thread_stats) total += ts.cache_misses;
        return total;
    }
};

#endif // COARSE_RENDERER_H