#ifndef FINEGRAINED_RENDERER_H
#define FINEGRAINED_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

// FinegrainedRenderer: modelo FGMT (Fine-Grained Multithreading).
//
// Simula 1 pipeline con NUM_THREADS contextos de hardware usando un
// RELOJ GLOBAL EXPLÏCITO: cada thread avanza solo cuando
//   global_clock_ % NUM_THREADS == thread_id
// Esto replica el round-robin obligatorio del hardware FGMT, donde el
// pipeline rota entre contextos en cada ciclo sin importar stalls.
//
// Comportamiento por ciclo asignado al thread:
//   Sin stall : COMPUTE — renderiza el pixel y avanza al siguiente
//                          (+PIXEL_QUANTUM_NS al VT del thread)
//   Con stall : NOP     — el slot se consume pero el pixel se reintenta
//                          (+NOP_PENALTY_NS; el stall completo queda oculto
//                           porque otro contexto ejecutara mientras espera)
//   IDLE      : el thread termino su tile pero sigue en el pipeline
//               para que los demas puedan obtener su turno
//                          (+NOP_PENALTY_NS por slot ocupado)
//
// Tiempo virtual = SUMA de los cuatro threads (pipeline compartido).
class FinegrainedRenderer : public IRenderer {
private:
    Scene scene;
    std::vector<Vector3>              frame;
    std::vector<CacheModel>           cache_models;
    std::vector<trace::ThreadMetrics> thread_stats;

    struct ThreadTile {
        int x_start, x_end;
        int y_start, y_end;
        int thread_id;
    };
    std::vector<ThreadTile> tiles;

    long long virtual_time_ns_ = 0LL;

    // Scheduler FGMT: reloj global compartido entre todos los threads.
    // Un thread solo avanza cuando global_clock_ % NUM_THREADS == su thread_id.
    // Esto serializa la ejecucion en ciclos de pipeline: 1 thread por ciclo.
    int global_clock_       = 0;
    int threads_completed_  = 0;
    std::mutex              pipeline_mutex_;
    std::condition_variable clock_tick_;

    void render_tile_worker(int thread_id);

public:
    // Constructor: inicializar renderer, crear tiles (2x2), cache models por thread
    // Divide el frame en 4 tiles iguales (NUM_THREADS=4), uno por thread
    // Inicializa CacheModel para cada thread con parámetros de Constants.h
    FinegrainedRenderer();

    // render_frame(): Implementación de IRenderer. Renderiza frame con 4 threads paralelos.
    // Responsabilidad:
    //   1. Resetear cache models y estadísticas de threads
    //   2. Crear 4 threads (uno por tile)
    //   3. Cada thread procesa su tile con cache modeling y NOPs
    //   4. Sincronizar (join) al finalizar todos los threads
    //   5. Retornar frame buffer con píxeles finales
    // Return: Vector<Vector3> con dimensiones IMAGE_WIDTH × IMAGE_HEIGHT
    std::vector<Vector3> render_frame() override;

    // get_model_name(): Retorna identificador del modelo para logging/CSV
    // Return: String "fgmt"
    std::string get_model_name() const override { return "fgmt"; }

    // get_thread_metrics(): Obtener estadísticas de threads de la última ejecución
    // Incluye: nops_count, nop_time_ns, cache_misses por cada thread
    // Return: Vector<ThreadMetrics> con una entrada por thread
    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const { return thread_stats; }

    // get_frame(): Acceder al frame procesado (para debugging o exportación manual)
    // Return: Vector<Vector3> con píxeles completamente renderizados
    const std::vector<Vector3>& get_frame() const { return frame; }
    long long get_virtual_time_ns() const override { return virtual_time_ns_; }
    int get_total_stalls() const override {
        int total = 0;
        for (const auto& ts : thread_stats) total += ts.cache_misses;
        return total;
    }
};

#endif // FINEGRAINED_RENDERER_H
