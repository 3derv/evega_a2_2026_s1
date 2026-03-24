#ifndef FINEGRAINED_RENDERER_H
#define FINEGRAINED_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

// FinegrainedRenderer: Renderizador FGMT (Fine-Grained Multithreading).
// Modela 1 pipeline compartido con 4 contextos en round-robin.
// Diferencias clave vs CGMT:
//   - Yield obligatorio tras CADA quantum (no solo en stall).
//   - Pixel con stall se difiere como "prefetched"; pipeline cede al siguiente contexto.
class FinegrainedRenderer : public IRenderer {
private:
    Scene scene;
    std::vector<Vector3>              frame;
    std::vector<CacheModel>           cache_models;
    std::vector<trace::ThreadMetrics> thread_stats;

    // Límites del tile asignado a cada thread
    struct ThreadTile {
        int x_start, x_end;
        int y_start, y_end;
        int thread_id;
    };
    std::vector<ThreadTile> tiles;

    // Cola de tareas por thread. prefetched=true → stall ya fue pagado, no reintentar cache.
    struct PixelTask { int x, y; bool prefetched; };
    std::vector<std::deque<PixelTask>> pixel_queues;

    // Scheduler round-robin: 1 pipeline, 4 contextos
    std::mutex              pipeline_mutex;
    std::condition_variable pipeline_cv;
    int                     pipeline_owner       = 0;
    int                     active_threads_count = 0;
    std::vector<bool>       thread_done_flags;

    long long virtual_time_ns_ = 0LL;

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
