#include "SMTRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include "RendererUtils.h"
#include <iomanip>
#include <sstream>

using namespace constants;
using namespace trace;

SMTRenderer::SMTRenderer()
    : scene_(), frame_(IMAGE_WIDTH * IMAGE_HEIGHT), global_clock_(0)
{
    tasks_.resize(SMT_NUM_THREADS);
    int total     = IMAGE_WIDTH * IMAGE_HEIGHT;
    int per_thread = total / SMT_NUM_THREADS;

    for (int i = 0; i < SMT_NUM_THREADS; ++i) {
        tasks_[i].start = i * per_thread;
        tasks_[i].end   = (i == SMT_NUM_THREADS - 1) ? total : (i + 1) * per_thread;
    }

    // Semilla determinista por thread (ver CacheModel.h)
    cache_models_.resize(SMT_NUM_THREADS);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        cache_models_[i] = CacheModel(CACHE_SIZE, 42u + static_cast<uint32_t>(i));

    thread_stats_.resize(SMT_NUM_THREADS);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        thread_stats_[i].thread_id = i;

    stall_ns_.resize(SMT_NUM_THREADS, 0LL);
    thread_finished_.resize(SMT_NUM_THREADS, false);
    log_stage_.resize(SMT_NUM_THREADS, 0);
    log_pixel_.resize(SMT_NUM_THREADS, 0);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        log_pixel_[i] = tasks_[i].start;
    // Semáforos inicializados en render_frame() (reinicio por frame)
}

// render_worker: ciclo de vida del contexto hardware SMT para el thread tid.
//
// Modela el "PC + archivo de registros" de un contexto SMT:
//   - pixel_idx / stage = contador de programa
//   - t_hit / hit_sphere = estado de registros del cómputo en vuelo
//
// Sincronización con semáforos (sin mutex en el camino crítico):
//   - sem_wait(&worker_sem_[tid]): duerme hasta ser seleccionado por el
//     coordinador. Solo los ≤ W threads despachados se despiertan por ciclo.
//   - Escribe stall_ns_[tid], thread_stats_, log_* (datos per-tid, sin race).
//   - sem_post(&done_sem_): señala al coordinador que terminó esta etapa.
//     La barrera del semáforo garantiza visibilidad de las escrituras anteriores.
void SMTRenderer::render_worker(int tid) {
    int    pixel_idx  = tasks_[tid].start;
    int    pixel_end  = tasks_[tid].end;
    int    stage      = 0;
    double t_hit      = std::numeric_limits<double>::infinity();
    int    hit_sphere = -1;

    while (true) {
        // ── ESPERA DESPACHO ────────────────────────────────────────────────
        // El coordinador hace sem_post(&worker_sem_[tid]) solo cuando selecciona
        // este thread como uno de los W slots del ciclo. Los demás threads
        // permanecen bloqueados sin ser despertados (0 spurious wakeups).
        sem_wait(&worker_sem_[tid]);
        if (coordinator_done_.load(std::memory_order_acquire)) break;

        // ── EJECUTAR ETAPA (sin lock — datos locales al thread) ────────────
        int x = pixel_idx % IMAGE_WIDTH;
        int y = pixel_idx / IMAGE_WIDTH;
        bool stalled = false;

        switch (stage) {
        case 0:
            // RAY_GEN: inicializar registros para nuevo pixel.
            // Equivale al decode/reorder buffer entry de un contexto SMT.
            // No accede a memoria externa → nunca stalla.
            t_hit      = std::numeric_limits<double>::infinity();
            hit_sphere = -1;
            stage      = 1;
            break;

        case 1: case 2: case 3: {
            // INTERSECT_k: accede a los datos de la esfera k en memoria.
            // Cada acceso es una potencial cache miss (acceso a struct Sphere).
            // Si hay miss → stall; el pipeline cede el slot a otro thread.
            // Si hay hit  → calcular intersección y actualizar registro t_hit.
            int k = stage - 1;
            if (cache_models_[tid].is_cache_miss(x, y)) {
                stalled = true;
                thread_stats_[tid].cache_misses++;
                // No avanzar stage: se reintentará cuando stall_ns_ llegue a 0
            } else {
                Ray    r = make_ray(x, y, camera_pos_);
                double t = 0.0;
                if (scene_.spheres[k].intersect(r, t) && t < t_hit) {
                    t_hit      = t;
                    hit_sphere = k;
                }
                stage++;
            }
            break;
        }

        case 4:
            // SHADE: determinar color final y escribir al framebuffer.
            // Usa hit_sphere (registro) para seleccionar color de la esfera más cercana.
            // Correctness: equivalente a Scene::trace() → imagen byte-exacta.
            frame_[pixel_idx] = (hit_sphere >= 0)
                ? scene_.spheres[hit_sphere].color
                : Vector3(0, 0, 0);
            stage     = 0;
            pixel_idx++;
            break;
        }

        // ── REPORTAR AL COORDINADOR (sin lock) ────────────────────────────
        // Solo este thread escribe sus campos per-tid → no hay race condition.
        // sem_post(&done_sem_) actúa como barrera de release: garantiza que
        // el coordinador vea estas escrituras al volver de sem_wait(&done_sem_).
        if (stalled) {
            stall_ns_[tid] = CACHE_MISS_PENALTY_NS;
        } else {
            thread_stats_[tid].virtual_time_ns += STAGE_QUANTUM_NS;
        }
        log_stage_[tid] = stage;
        log_pixel_[tid] = pixel_idx;
        if (pixel_idx >= pixel_end)
            thread_finished_[tid] = true;

        sem_post(&done_sem_);   // avisar al coordinador: etapa completada

        if (pixel_idx >= pixel_end) break;
    }
}

// render_frame: Coordinador del scheduler SMT.
//
// Ejecuta el ciclo de hardware: por cada ciclo selecciona hasta W threads
// "listos" (stall_ns_ == 0, !finished) en round-robin y los despacha
// simultáneamente. Después de que todos completan su etapa, decrementa los
// stall counters de todos los threads vivos y avanza el reloj global.
//
// La clave diferenciadora vs FGMT: cuando 0 < W threads están listos y
// otro está en stall, el slot del thread stallado lo ocupa un thread activo.
// Esto reduce el VT total porque stalls no generan quanta desperdiciados.
std::vector<Vector3> SMTRenderer::render_frame() {
    // Reiniciar cache models, estadísticas y estado del dispatcher
    reset_thread_stats(thread_stats_, cache_models_);
    for (int i = 0; i < SMT_NUM_THREADS; ++i) {
        stall_ns_[i]        = 0LL;
        thread_finished_[i] = false;
        log_stage_[i]       = 0;
        log_pixel_[i]       = tasks_[i].start;
    }
    coordinator_done_.store(false, std::memory_order_relaxed);
    global_clock_ = 0;

    // Inicializar semáforos:
    //   worker_sem_[i] = 0 → cada thread arranca bloqueado esperando despacho
    //   done_sem_      = 0 → coordinador bloqueará hasta que workers reporten
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        sem_init(&worker_sem_[i], /*pshared=*/0, /*value=*/0);
    sem_init(&done_sem_, /*pshared=*/0, /*value=*/0);

    // Iniciar N threads (contextos hardware)
    std::vector<std::thread> workers;
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        workers.emplace_back(&SMTRenderer::render_worker, this, i);

    if (verbose_cycles_ > 0) {
        std::cout << "\n[SMT VERBOSE] issue_width=" << SMT_ISSUE_WIDTH
                  << "  threads=" << SMT_NUM_THREADS
                  << "  stage_quantum=" << STAGE_QUANTUM_NS << "ns\n";
        std::cout << std::string(72, '-') << "\n";
    }

    // ── CICLO DE HARDWARE ──────────────────────────────────────────────────
    // Cada iteración = 1 ciclo de reloj simulado.
    // El coordinador es el "control unit" que gestiona el issue de instrucciones.
    int last_dispatched = SMT_NUM_THREADS - 1;

    while (true) {
        // ── VERIFICAR TERMINACIÓN ─────────────────────────────────────────
        // Todos los sem_wait(&done_sem_) del ciclo anterior ya completaron,
        // por lo que las escrituras a thread_finished_[] son visibles aquí.
        int n_done = 0;
        for (bool f : thread_finished_) if (f) ++n_done;
        if (n_done == SMT_NUM_THREADS) {
            // Broadcast: despertar todos los workers bloqueados en worker_sem_
            coordinator_done_.store(true, std::memory_order_release);
            for (int i = 0; i < SMT_NUM_THREADS; ++i)
                sem_post(&worker_sem_[i]);
            break;
        }

        // ── DESPACHAR ≤ W THREADS LISTOS ─────────────────────────────────
        // "Listo" = tile no terminado y sin stall pendiente.
        // sem_post punto a punto: solo los threads seleccionados se despiertan.
        int n_dispatched = 0;
        int scan = (last_dispatched + 1) % SMT_NUM_THREADS;
        for (int i = 0; i < SMT_NUM_THREADS && n_dispatched < SMT_ISSUE_WIDTH; ++i) {
            int tid = (scan + i) % SMT_NUM_THREADS;
            if (!thread_finished_[tid] && stall_ns_[tid] <= 0) {
                sem_post(&worker_sem_[tid]);  // 1 syscall, 0 spurious wakeups
                last_dispatched = tid;
                ++n_dispatched;
            }
        }

        // ── ESPERAR COMPLETIONS ───────────────────────────────────────────
        // done_sem_ es un semáforo contador: cada worker hace sem_post al
        // terminar su etapa. Esperamos exactamente n_dispatched veces.
        // Si n_dispatched==0 (todos en stall) no bloqueamos → drain stalls.
        for (int i = 0; i < n_dispatched; ++i)
            sem_wait(&done_sem_);

        // Después de los sem_wait, todas las escrituras de los workers
        // despachados (stall_ns_, thread_finished_, log_*) son visibles.
        for (int i = 0; i < SMT_NUM_THREADS; ++i) {
            if (!thread_finished_[i] && stall_ns_[i] > 0)
                stall_ns_[i] = std::max(0LL, stall_ns_[i] - STAGE_QUANTUM_NS);
        }

        // ── LOGGING VERBOSE ───────────────────────────────────────────────
        if (verbose_cycles_ > 0 && global_clock_ < verbose_cycles_) {
            std::cout << "[Ciclo " << std::setw(4) << global_clock_ << "] ";
            for (int i = 0; i < SMT_NUM_THREADS; ++i) {
                std::cout << "T" << i << ":";
                if (thread_finished_[i]) {
                    std::cout << "DONE";
                } else if (stall_ns_[i] > 0) {
                    std::cout << "STALL(" << stall_ns_[i] << "ns)";
                } else {
                    int px = log_pixel_[i];
                    int st = log_stage_[i];
                    std::cout << STAGE_NAMES[st]
                              << "(p=" << px
                              << ",x=" << (px % IMAGE_WIDTH)
                              << ",y=" << (px / IMAGE_WIDTH) << ")";
                }
                if (i < SMT_NUM_THREADS - 1) std::cout << "  ";
            }
            std::cout << "\n";
        }

        global_clock_++;
    }

    for (auto& t : workers) t.join();

    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        sem_destroy(&worker_sem_[i]);
    sem_destroy(&done_sem_);

    // VT total = suma de threads (pipeline compartido; stalls hidden por otros threads)
    virtual_time_ns_ = sum_virtual_times(thread_stats_);
    return frame_;
}
