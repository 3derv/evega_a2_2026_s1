#ifndef FINEGRAINED_RENDERER_H
#define FINEGRAINED_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include "Ray.h"
#include "Constants.h"
#include "SchedulerLogger.h"
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
        int start, end;    // Rango lineal de píxeles [start, end)
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
    // global_cycle_: cuenta cuántos slots de pipeline se han despachado.
    // Incrementado atómicamente por el thread activo al tomar el semáforo.
    // Serializado de facto por el protocolo de semáforos (un thread activo).
    std::atomic<int>         global_cycle_{0};
    SchedulerLogger          logger_;   // Traza ciclo-a-ciclo (activar con set_verbose)

    void render_tile_worker(int thread_id);

public:
    // Constructor: divide el frame en 4 tiles 2×2 (uno por thread) e inicializa
    // los CacheModel con semillas deterministas (base 42 + thread_id).
    FinegrainedRenderer();

    // Actualiza la posición de cámara antes de render_frame().
    // GenericRunner la llama una vez por frame; el scheduler FGMT no cambia.
    void set_camera_pos(const Vector3& pos) override { camera_pos_ = pos; }

    // Habilita la traza del scheduler para los primeros `cycles` ciclos de pipeline.
    void set_verbose(int cycles) override { logger_.set_max_cycles(cycles); }

    // render_frame(): resetea estado, lanza NUM_THREADS threads (uno por tile) y
    // espera a que todos terminen. VT = suma de VTs por thread (pipeline compartido).
    std::vector<Vector3> render_frame() override;

    // get_model_name(): identifica el modelo para logging/CSV
    std::string get_model_name() const override { return "fgmt"; }

    // get_thread_metrics(): estadísticas (misses, VT) de los 4 threads del último frame.
    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const override { return thread_stats; }

    // get_frame(): frame procesado (para debugging o exportación manual)
    const std::vector<Vector3>& get_frame() const { return frame; }
    long long get_virtual_time_ns() const override { return virtual_time_ns_; }
    int get_total_stalls() const override {
        int total = 0;
        for (const auto& ts : thread_stats) total += ts.cache_misses;
        return total;
    }
};

#endif // FINEGRAINED_RENDERER_H
