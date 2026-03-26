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

    cache_models_.resize(SMT_NUM_THREADS, CacheModel(CACHE_SIZE));

    thread_stats_.resize(SMT_NUM_THREADS);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        thread_stats_[i].thread_id = i;

    dispatch_flags_.resize(SMT_NUM_THREADS, false);
    stall_ns_.resize(SMT_NUM_THREADS, 0LL);
    thread_finished_.resize(SMT_NUM_THREADS, false);
    log_stage_.resize(SMT_NUM_THREADS, 0);
    log_pixel_.resize(SMT_NUM_THREADS, 0);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        log_pixel_[i] = tasks_[i].start;
}

// render_worker: ciclo de vida del contexto hardware SMT para el thread tid.
//
// Modela el "PC + archivo de registros" de un contexto SMT:
//   - pixel_idx / stage = contador de programa (qué instrucción ejecutar a continuación)
//   - t_hit / hit_sphere = estado de registros del cómputo en vuelo
//
// Por cada despacho del coordinador, ejecuta UNA etapa del pipeline:
//   RAY_GEN     → inicializa registros; nunca stalla
//   INTERSECT_k → accede a datos de esfera k (puede causar cache miss → stall)
//   SHADE       → escribe pixel al framebuffer; nunca stalla; avanza pixel_idx
//
// En stall: se informa a través de stall_ns_[tid] = CACHE_MISS_PENALTY_NS.
//           El coordinador decrementa stall_ns_ cada ciclo hasta que llega a 0.
//           Mientras stall_ns_[tid] > 0, el coordinador NO despacha este thread,
//           asignando su slot a otro thread con trabajo real (ventaja SMT).
//           El stall NO acumula VT en este thread (el slot lo "usa" otro thread).
//
// En compute: acumula STAGE_QUANTUM_NS en virtual_time_ns (trabajo productivo).
void SMTRenderer::render_worker(int tid) {
    int    pixel_idx = tasks_[tid].start;
    int    pixel_end = tasks_[tid].end;
    int    stage     = 0;
    double t_hit     = std::numeric_limits<double>::infinity();
    int    hit_sphere = -1;

    while (true) {
        // ── ESPERA DESPACHO ────────────────────────────────────────────────
        // Bloquearse hasta que el coordinador asigne este slot de issue.
        // Espejo del fetch/dispatch del hardware SMT: el thread espera en la
        // cola de instrucciones hasta que la unidad de issue lo selecciona.
        {
            std::unique_lock<std::mutex> ul(dispatch_mutex_);
            dispatch_cv_.wait(ul, [&] {
                return dispatch_flags_[tid] || coordinator_done_;
            });
            if (coordinator_done_) break;
            dispatch_flags_[tid] = false;
        }

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

        // ── REPORTAR AL COORDINADOR ────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lg(dispatch_mutex_);

            if (stalled) {
                // Stall: programar penalización. El coordinador decrementará
                // stall_ns_ cada ciclo hasta llegar a 0 (CACHE_MISS_PENALTY_NS/STAGE_QUANTUM_NS
                // = 16 ciclos de espera). Durante ese tiempo otro thread llena el slot.
                stall_ns_[tid] = CACHE_MISS_PENALTY_NS;
            } else {
                // Productivo: acumular STAGE_QUANTUM_NS al VT de este thread.
                // Solo el trabajo real cuenta; los ciclos en stall no penalizan VT
                // porque son ocultados por el trabajo de otro thread.
                thread_stats_[tid].virtual_time_ns += STAGE_QUANTUM_NS;
            }
            // Actualizar estado de logging para que el coordinador lo muestre
            log_stage_[tid] = stage;
            log_pixel_[tid] = pixel_idx;
            if (pixel_idx >= pixel_end)
                thread_finished_[tid] = true;

            slots_completed_++;
        }
        dispatch_cv_.notify_all();

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
        dispatch_flags_[i]   = false;
        stall_ns_[i]         = 0LL;
        thread_finished_[i]  = false;
        log_stage_[i]        = 0;
        log_pixel_[i]        = tasks_[i].start;
    }
    slots_to_complete_ = 0;
    slots_completed_   = 0;
    coordinator_done_  = false;
    global_clock_      = 0;

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
    int last_dispatched = SMT_NUM_THREADS - 1;  // Posición inicial del round-robin

    while (true) {
        // Verificar si todos los threads completaron sus tiles
        {
            std::lock_guard<std::mutex> lg(dispatch_mutex_);

            int n_done = 0;
            for (bool f : thread_finished_) if (f) ++n_done;
            if (n_done == SMT_NUM_THREADS) {
                coordinator_done_ = true;
                break;  // salir antes de notify para evitar double-notify
            }

            // Seleccionar hasta W threads listos: round-robin desde last_dispatched.
            // "Listo" = no stallado y tile no completado.
            // Esto modela la lógica de selección de instrucciones del SMT issue unit.
            int n_dispatched = 0;
            int scan = (last_dispatched + 1) % SMT_NUM_THREADS;
            for (int i = 0; i < SMT_NUM_THREADS && n_dispatched < SMT_ISSUE_WIDTH; ++i) {
                int tid = (scan + i) % SMT_NUM_THREADS;
                if (!thread_finished_[tid] && stall_ns_[tid] <= 0) {
                    dispatch_flags_[tid] = true;
                    last_dispatched = tid;
                    ++n_dispatched;
                }
            }

            slots_to_complete_ = n_dispatched;
            slots_completed_   = 0;
        }
        dispatch_cv_.notify_all();

        // Esperar que todos los threads despachados completen su etapa.
        // Si slots_to_complete_ == 0 (todos en stall), la condición es verdadera
        // inmediatamente → el ciclo avanza sin bloqueo (drain stalls).
        {
            std::unique_lock<std::mutex> ul(dispatch_mutex_);
            dispatch_cv_.wait(ul, [&] {
                return slots_completed_ >= slots_to_complete_;
            });

            // Decrementar stall counters de todos los threads vivos.
            // Ocurre DESPUÉS de que los threads despachados completan su etapa,
            // garantizando que el contador refleja el número correcto de ciclos pasados.
            for (int i = 0; i < SMT_NUM_THREADS; ++i) {
                if (!thread_finished_[i] && stall_ns_[i] > 0)
                    stall_ns_[i] = std::max(0LL, stall_ns_[i] - STAGE_QUANTUM_NS);
            }

            // ── LOGGING VERBOSE ─────────────────────────────────────────────
            // Se imprime TRAS el ciclo (etapa ya ejecutada, stalls ya decrementados).
            // Formato: [Ciclo N] T0:ETAPA(p=X,stall=Xns) T1:STALL(Xns) T2:DONE
            if (verbose_cycles_ > 0 && global_clock_ < verbose_cycles_) {
                std::cout << "[Ciclo " << std::setw(4) << global_clock_ << "] ";
                for (int i = 0; i < SMT_NUM_THREADS; ++i) {
                    std::cout << "T" << i << ":";
                    if (thread_finished_[i]) {
                        std::cout << "DONE";
                    } else if (stall_ns_[i] > 0) {
                        // El counter ya fue decrementado; el stall_ns_ es el restante
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
    }

    // Despertar workers bloqueados en la espera de despacho (coordinator_done_ = true)
    dispatch_cv_.notify_all();
    for (auto& t : workers) t.join();

    // VT total = suma de threads (pipeline compartido; stalls hidden por otros threads)
    virtual_time_ns_ = sum_virtual_times(thread_stats_);
    return frame_;
}
