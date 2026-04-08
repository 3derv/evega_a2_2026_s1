#include "SMTRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include "RendererUtils.h"
#include <limits>

using namespace constants;
using namespace trace;

SMTRenderer::SMTRenderer()
    : scene_(), frame_(IMAGE_WIDTH * IMAGE_HEIGHT), global_clock_(0)
{
    const int total      = IMAGE_WIDTH * IMAGE_HEIGHT;
    const int per_thread = total / SMT_NUM_THREADS;

    tasks_.resize(SMT_NUM_THREADS);
    for (int i = 0; i < SMT_NUM_THREADS; ++i) {
        tasks_[i].start = i * per_thread;
        tasks_[i].end   = (i == SMT_NUM_THREADS - 1) ? total : (i + 1) * per_thread;
    }

    // Semilla determinista por thread: 42+tid garantiza reproducibilidad
    cache_models_.resize(SMT_NUM_THREADS);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        cache_models_[i] = CacheModel(CACHE_SIZE, 42u + static_cast<uint32_t>(i));

    thread_stats_.resize(SMT_NUM_THREADS);
    for (int i = 0; i < SMT_NUM_THREADS; ++i)
        thread_stats_[i].thread_id = i;

    pixel_idx_.resize(SMT_NUM_THREADS, 0);
    stall_countdown_.resize(SMT_NUM_THREADS, 0);
    thread_finished_.resize(SMT_NUM_THREADS, false);
    pending_stall_.resize(SMT_NUM_THREADS, false);
}

// render_pixel: función auxiliar que produce el color de un píxel completo.
// Sin descomposición en etapas: correctness garantizado (equivalente a Scene::trace()).
static Vector3 render_pixel(const Scene& scene, int x, int y, const Vector3& cam) {
    Ray    r    = make_ray(x, y, cam);
    double tmin = std::numeric_limits<double>::infinity();
    int    hit  = -1;
    for (int k = 0; k < static_cast<int>(scene.spheres.size()); ++k) {
        double t = 0.0;
        if (scene.spheres[k].intersect(r, t) && t < tmin) { tmin = t; hit = k; }
    }
    return (hit >= 0) ? scene.spheres[hit].color : Vector3(0, 0, 0);
}

// render_frame: simulación SMT pura por píxel (sin OS threads, sin semáforos).
//
// Modelo de hardware:
//   - SMT_ISSUE_WIDTH=2 slots que emiten en paralelo cada ciclo.
//   - SMT_NUM_THREADS=4 contextos, cada uno con su propio PC (pixel_idx_)
//     y CacheModel — modela el "archivo de registros" independiente de SMT.
//
// Regla de ocupación del slot:
//   1. Thread entra al slot y lanza su SIGUIENTE píxel.
//   2. Cache HIT  → píxel renderizado completo; thread acumula PIXEL_QUANTUM_NS
//      de VT y avanza su PC.  El slot queda productivo.
//   3. Cache MISS → thread es EYECTADO del slot inmediatamente.
//      El slot sigue disponible: el siguiente thread listo (round-robin) entra.
//      Stall completamente oculto: 0 VT desperdiciado.
//   4. Thread eyectado espera stall_countdown_ ciclos antes de volver
//      a la cola de listos (= CACHE_MISS_PENALTY_NS / PIXEL_QUANTUM_NS ciclos).
//
// De esta forma la ventana de emisión W=2 siempre se llena con trabajo real
// mientras existan threads listos, ocultando latencias de cache de forma nativa.
std::vector<Vector3> SMTRenderer::render_frame() {
    reset_thread_stats(thread_stats_, cache_models_);
    for (int i = 0; i < SMT_NUM_THREADS; ++i) {
        pixel_idx_[i]       = tasks_[i].start;
        stall_countdown_[i] = 0;
        thread_finished_[i] = false;
        pending_stall_[i]   = false;
    }
    global_clock_ = 0;

    logger_.log_header("smt", SMT_NUM_THREADS, SMT_ISSUE_WIDTH,
                      PIXEL_QUANTUM_NS, CACHE_MISS_PENALTY_NS);

    // rr: puntero round-robin — de dónde empezamos a escanear en el próximo ciclo.
    // Se avanza al (último thread que llenó un slot + 1) para equilibrar la carga.
    int rr = 0;

    while (true) {
        // ── TERMINACIÓN ───────────────────────────────────────────────────
        int n_done = 0;
        for (bool f : thread_finished_) if (f) ++n_done;
        if (n_done == SMT_NUM_THREADS) break;

        // ── LLENAR HASTA W SLOTS ──────────────────────────────────────────
        // scan_rr fijo durante el ciclo: evita que la actualización de rr
        // dentro del bucle perturbe los índices de los intentos posteriores.
        const int scan_rr   = rr;
        int       slots_filled = 0;

        for (int attempt = 0;
             attempt < SMT_NUM_THREADS && slots_filled < SMT_ISSUE_WIDTH;
             ++attempt)
        {
            int tid = (scan_rr + attempt) % SMT_NUM_THREADS;
            if (thread_finished_[tid] || stall_countdown_[tid] > 0) continue;

            int px = pixel_idx_[tid];
            int x  = px % IMAGE_WIDTH;
            int y  = px / IMAGE_WIDTH;

            if (!pending_stall_[tid] && cache_models_[tid].is_cache_miss(x, y)) {
                // MISS: thread eyectado. El slot sigue disponible para el
                // siguiente thread listo (el bucle continúa hacia attempt+1).
                // El stall queda oculto: 0 VT desperdiciado por este hilo.
                thread_stats_[tid].cache_misses++;
                stall_countdown_[tid] = CACHE_MISS_PENALTY_NS / PIXEL_QUANTUM_NS;
                pending_stall_[tid] = true;
                logger_.log_stall(global_clock_, tid, x, y, 0LL, "miss→ejected");
            } else {
                // HIT (o dato ya en cache tras stall): slot ocupado productivamente.
                frame_[px] = render_pixel(scene_, x, y, camera_pos_);
                thread_stats_[tid].virtual_time_ns += PIXEL_QUANTUM_NS;
                pending_stall_[tid] = false;
                logger_.log_compute(global_clock_, tid, x, y, PIXEL_QUANTUM_NS);
                pixel_idx_[tid]++;
                if (pixel_idx_[tid] >= tasks_[tid].end) {
                    thread_finished_[tid] = true;
                    logger_.log_done(global_clock_, tid);
                }
                ++slots_filled;
                rr = (tid + 1) % SMT_NUM_THREADS; // avanzar round-robin
            }
        }

        // ── DECREMENTAR STALL COUNTDOWNS ─────────────────────────────────
        // Cada ciclo (= PIXEL_QUANTUM_NS) reduce el tiempo de penalización.
        for (int i = 0; i < SMT_NUM_THREADS; ++i)
            if (!thread_finished_[i] && stall_countdown_[i] > 0)
                --stall_countdown_[i];

        ++global_clock_;
    }

    // VT = reloj de pared de la simulación: ciclos_totales × PIXEL_QUANTUM_NS.
    //
    // NO usar sum_virtual_times() aquí: la suma de VT por thread siempre da
    // total_píxeles × PIXEL_QUANTUM_NS independientemente de W, porque cada
    // thread acumula 1000 ns por cada pixel que termina y la suma total de
    // píxeles completados siempre es total_píxeles.
    //
    // Con W=2, el pipeline completa ~2 píxeles por ciclo → la simulación
    // termina en ~total_píxeles/W ciclos → VT ≈ (total_píxeles/W) × Q.
    // Esto refleja el speedup real de la emisión simultánea (Amdahl: ~2×).
    //
    // La suma de VT por thread se conserva en thread_stats_ para el log
    // per-thread (stalls, píxeles procesados por contexto), pero el VT
    // reportado al sistema (get_virtual_time_ns) usa el reloj de pared.
    virtual_time_ns_ = static_cast<long long>(global_clock_) * PIXEL_QUANTUM_NS;
    return frame_;
}
