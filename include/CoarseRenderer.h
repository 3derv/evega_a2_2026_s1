#ifndef COARSE_RENDERER_H
#define COARSE_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include "Ray.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// CoarseRenderer: Renderizador CGMT (Coarse-Grained Multithreading).
//
// Scheduler: solo un thread ejecuta a la vez. La rotación ocurre
// ÚNICAMENTE cuando el thread activo detecta un stall (cache miss).
// Esto difiere de FGMT, donde la rotación es obligatoria cada ciclo.
//
// Mecanismo (idéntico al código de referencia en C):
//   - current_thread_ indica qué thread tiene el pipeline.
//   - En STALL: el thread CEDE el slot INMEDIATAMENTE al siguiente ready.
//        → stats: 0 ns de VT (stall completamente oculto).
//        → el pixel NO avanza; se reintentará cuando recupere el slot.
//        → el otro thread usa el slot → ningún ciclo desperdiciado.
//   - En COMPUTE: se renderiza el pixel, se avanza, sin cambio de contexto.
//        → stats: +PIXEL_QUANTUM_NS
//   - Al terminar el tile: switch_to_next_thread() sin coste extra.
//
// switch_to_next_thread() está extraída como método privado (DRY + SRP)
// para evitar duplicar la búsqueda del siguiente thread activo.
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
    
    // Variables de scheduler CGMT
    std::mutex sched_mutex;
    std::condition_variable sched_cv;
    int current_thread;                // Thread que tiene asignado el pipeline
    std::vector<bool> thread_done;     // true si el thread completó su tile
    int threads_finished;              // Conteo de threads terminados (bajo mutex)
    int global_clock_;                 // Ciclos totales de hardware simulados

    long long virtual_time_ns_ = 0LL;

    // Posición de cámara para el frame actual (actualizada por GenericRunner).
    // Permite recibir la órbita elíptica sin modificar el scheduler CGMT.
    Vector3 camera_pos_;

    // switch_to_next_thread(): Scheduler hardware CGMT.
    // Busca el siguiente thread activo (round-robin, saltando los terminados).
    // Debe llamarse mientras se sostiene sched_mutex.
    void switch_to_next_thread();
    void render_worker(int thread_id);

public:
    CoarseRenderer();

    // Actualiza la posición de cámara antes de render_frame().
    // GenericRunner la llama una vez por frame; el scheduler CGMT no cambia.
    void set_camera_pos(const Vector3& pos) override { camera_pos_ = pos; }

    std::vector<Vector3> render_frame() override;
    std::string get_model_name() const override { return "cgmt"; }
    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const override {
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