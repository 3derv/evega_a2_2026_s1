#ifndef SMT_RENDERER_H
#define SMT_RENDERER_H

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

// SMTRenderer: Renderizador SMT (Simultaneous Multithreading).
//
// Modela un procesador con SMT_ISSUE_WIDTH=2 slots de issue simultáneos.
// A diferencia de FGMT (1 slot/ciclo, rotación obligatoria) y CGMT (1 slot/ciclo,
// rotación solo en stall), SMT puede DESPACHAR instrucciones de hasta W=2
// threads distintos en el MISMO ciclo.
//
// Descomposición del pixel en 5 etapas (equivale al PC + archivo de registros):
//   RAY_GEN      (0) — construye el rayo; sin posibilidad de stall
//   INTERSECT_0  (1) — testea esfera 0; puede stallarse (cache miss)
//   INTERSECT_1  (2) — testea esfera 1; puede stallarse
//   INTERSECT_2  (3) — testea esfera 2; puede stallarse
//   SHADE        (4) — determina color y escribe pixel; sin stall
//
// Cada etapa cuesta STAGE_QUANTUM_NS = 200 ns.
// Un pixel completo sin stalls = 5 × 200 = 1000 ns → igual que otros modelos.
//
// Ventaja sobre FGMT/CGMT: cuando thread A stalla en INTERSECT, el slot liberado
// es ocupado por thread B con TRABAJO REAL (no NOP). Esto reduce el VT total
// porque stalls no se contabilizan como tiempo perdido sino como tiempo cedido.
//
// VT por thread: solo se acumula en etapas productivas (sin stall).
// Un stall no añade VT al thread que lo causa; el VT lo genera quien llena su slot.
//
// Scheduler (coordinador en render_frame):
//   Por ciclo: seleccionar hasta W threads listos (stall_ns ≤ 0, !finished)
//   en round-robin desde el último despachado.
//   Señalización punto a punto con semáforos:
//     - sem_post(&worker_sem_[tid]) por cada thread despachado (máx W=2)
//     - sem_wait(&done_sem_) × n_dispatched para esperar completions
//   Elimina los spurious wakeups de notify_all(): solo los threads
//   seleccionados se despiertan; los demás duermen sin ser tocados.
class SMTRenderer : public IRenderer {
public:
    SMTRenderer();

    // Habilitar logging ciclo a ciclo. cycles = número de ciclos a imprimir (0 = off).
    void set_verbose(int cycles) { verbose_cycles_ = cycles; }

    // Actualiza la posición de cámara antes de render_frame().
    // GenericRunner la llama una vez por frame; el scheduler SMT no cambia.
    void set_camera_pos(const Vector3& pos) override { camera_pos_ = pos; }

    std::vector<Vector3> render_frame() override;

    std::string get_model_name() const override { return "smt"; }

    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const override {
        return thread_stats_;
    }

    long long get_virtual_time_ns() const override { return virtual_time_ns_; }

    int get_total_stalls() const override {
        int total = 0;
        for (const auto& ts : thread_stats_) total += ts.cache_misses;
        return total;
    }

private:
    // Rango de píxeles asignado a cada thread (equivale al "tile" de FGMT/CGMT)
    struct Task { int start, end; };

    Scene   scene_;
    Vector3 camera_pos_;   // Posición de cámara para el frame actual (órbita elíptica)
    std::vector<Vector3>              frame_;
    std::vector<CacheModel>           cache_models_;
    std::vector<trace::ThreadMetrics> thread_stats_;
    std::vector<Task>                 tasks_;

    long long virtual_time_ns_ = 0LL;
    int       global_clock_    = 0;

    // ── Estado del dispatcher SMT (semáforos) ──────────────────────────────
    //
    // worker_sem_[i]: el coordinador hace sem_post para despachar thread i.
    //   Thread i bloquea en sem_wait hasta recibir su asignación de issue.
    //   Solo los threads seleccionados (≤ W=2) se despiertan por ciclo.
    //
    // done_sem_: semáforo contador. Cada worker hace sem_post al terminar
    //   su etapa. El coordinador hace sem_wait × n_dispatched para esperar
    //   exactamente las completions del ciclo actual (sin falsos despertares).
    //
    // stall_ns_[i] y thread_finished_[i]: escritos por worker i ANTES de
    //   sem_post(&done_sem_), leídos por coordinador DESPUÉS de sem_wait.
    //   La barrera del semáforo garantiza visibilidad sin mutex adicional.
    sem_t worker_sem_[constants::SMT_NUM_THREADS]; // coordinador → worker
    sem_t done_sem_;                                // worker → coordinador

    std::vector<long long>       stall_ns_;         // stall restante por thread (ns)
    std::vector<bool>            thread_finished_;  // tile completado por thread
    std::atomic<bool>            coordinator_done_{false}; // señal de fin a workers

    // ── Logging verbose ───────────────────────────────────────────────────
    // Actualizado por cada worker bajo dispatch_mutex_ al terminar su etapa.
    // El coordinador lo lee al inicio del ciclo siguiente para imprimir el log.
    int              verbose_cycles_ = 0;  // 0 = off; N = imprimir primeros N ciclos
    std::vector<int> log_stage_;           // etapa actual de cada thread
    std::vector<int> log_pixel_;           // pixel_idx actual de cada thread
    // Etiquetas legibles para las etapas del pipeline
    static constexpr const char* STAGE_NAMES[] = {
        "RAY_GEN", "INTERSECT_0", "INTERSECT_1", "INTERSECT_2", "SHADE"
    };

    // render_worker: ciclo de vida de cada contexto hardware SMT.
    // Espera dispatch_flags_[tid], ejecuta su etapa actual (RAY_GEN/INTERSECT/SHADE),
    // reporta stall si hubo cache miss, acumula VT si fue productivo.
    void render_worker(int tid);
};

#endif // SMT_RENDERER_H
