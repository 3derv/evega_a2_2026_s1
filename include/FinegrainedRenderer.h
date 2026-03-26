#ifndef FINEGRAINED_RENDERER_H
#define FINEGRAINED_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include "Ray.h"
#include "Constants.h"
#include <vector>
#include <thread>
#include <atomic>
#include <semaphore.h>

// FinegrainedRenderer: modelo FGMT (Fine-Grained Multithreading).
//
// Simula 1 pipeline con NUM_THREADS contextos de hardware.
// Scheduler: semáforo por thread — cada thread espera en su propio
// sem_wait() y, al terminar su ciclo, señala directamente al siguiente
// thread con píxeles pendientes (skip de IDLE).
//
// Esto elimina los spurious wakeups que generaba notify_all():
//   Antes : notify_all() → wakeup × NUM_THREADS × 192 000 ciclos
//   Ahora : sem_post(next) → 1 wakeup × ciclos_activos
//
// Comportamiento por ciclo:
//   Sin stall : COMPUTE — renderiza el pixel y avanza (+PIXEL_QUANTUM_NS)
//   Con stall : NOP     — stall oculto; otro contexto toma el pipeline
//                          (+NOP_PENALTY_NS)
//   IDLE      : el thread duerme hasta broadcast final (no consume VT)
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

    // Posición de cámara para el frame actual (actualizada por GenericRunner).
    // Permite que el scheduler reciba la órbita elíptica sin cambiar su lógica.
    Vector3 camera_pos_;

    // Scheduler FGMT: semáforo por thread para señalización punto a punto.
    // slots_[i]: thread i espera aquí su turno de pipeline.
    // tile_done_[i]: true cuando thread i terminó todos sus píxeles.
    // threads_completed_: contador atómico; al llegar a NUM_THREADS el
    //   último thread hace broadcast para desbloquear los demás.
    sem_t                    slots_[constants::NUM_THREADS];
    std::atomic<int>         threads_completed_{0};
    std::atomic<bool>        tile_done_[constants::NUM_THREADS];

    void render_tile_worker(int thread_id);

public:
    // Constructor: inicializar renderer, crear tiles (2x2), cache models por thread
    // Divide el frame en 4 tiles iguales (NUM_THREADS=4), uno por thread
    // Inicializa CacheModel para cada thread con parámetros de Constants.h
    FinegrainedRenderer();

    // Actualiza la posición de cámara antes de render_frame().
    // GenericRunner la llama una vez por frame; el scheduler FGMT no cambia.
    void set_camera_pos(const Vector3& pos) override { camera_pos_ = pos; }

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
