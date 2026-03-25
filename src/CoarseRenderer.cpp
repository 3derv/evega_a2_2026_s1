#include "CoarseRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include "RendererUtils.h"

using namespace constants;
using namespace trace;

CoarseRenderer::CoarseRenderer()
    : scene(), frame(IMAGE_WIDTH * IMAGE_HEIGHT),
      current_thread(0), threads_finished(0), global_clock_(0) {

    tasks.resize(NUM_THREADS);
    int total_pixels    = IMAGE_WIDTH * IMAGE_HEIGHT;
    int pixels_per_thread = total_pixels / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; ++i) {
        tasks[i].start     = i * pixels_per_thread;
        tasks[i].end       = (i == NUM_THREADS - 1) ? total_pixels
                                                     : (i + 1) * pixels_per_thread;
        tasks[i].thread_id = i;
    }

    cache_models.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i)
        cache_models[i] = CacheModel(CACHE_SIZE);

    thread_done.resize(NUM_THREADS, false);

    thread_stats.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i)
        thread_stats[i].thread_id = i;
}

// switch_to_next_thread: scheduler hardware CGMT.
//
// Busca el siguiente thread activo en round-robin saltando los que ya
// terminaron su tile. Debe llamarse bajo sched_mutex.
// Espejo del switch_to_next_thread() del código de referencia en C.
void CoarseRenderer::switch_to_next_thread() {
    int next = (current_thread + 1) % NUM_THREADS;
    while (thread_done[next] && threads_finished < NUM_THREADS)
        next = (next + 1) % NUM_THREADS;
    current_thread = next;
}

// render_worker: ciclo principal del scheduler CGMT.
//
// Replica el patrón del código de referencia en C:
//   1. ESPERA  — bloquea hasta que el scheduler asigne este thread.
//   2a. STALL  — cache miss: no avanza el pixel, paga el ciclo de stall
//                y si hubo cambio real de contexto paga la penalización.
//   2b. COMPUTE — sin stall: renderiza el pixel y avanza al siguiente.
//                 Si terminó el tile: marca done y cede sin costo extra.
//   3. BROADCAST — notifica a todos para que el siguiente despierte.
//
// Diferencia clave vs FGMT: la rotación solo ocurre en stall, no en
// cada ciclo. El thread retiene el pipeline mientras no tenga stalls.
void CoarseRenderer::render_worker(int thread_id) {
    const Task&    task  = tasks[thread_id];
    CacheModel&    cache = cache_models[thread_id];
    ThreadMetrics& stats = thread_stats[thread_id];

    int pixel_idx = task.start;

    while (true) {
        std::unique_lock<std::mutex> lock(sched_mutex);

        // Salida anticipada: todos los tiles completados
        if (threads_finished == NUM_THREADS) break;

        // CGMT: esperar a que el hardware asigne este contexto
        sched_cv.wait(lock, [&] {
            return current_thread == thread_id
                || threads_finished == NUM_THREADS;
        });

        if (threads_finished == NUM_THREADS) break;

        int x = pixel_idx % IMAGE_WIDTH;
        int y = pixel_idx / IMAGE_WIDTH;

        if (cache.is_cache_miss(x, y)) {
            // ── STALL ──────────────────────────────────────────────────────
            // El pipeline detecta el miss: el slot se consume pero el pixel
            // NO avanza (se reintentará el próximo turno de este contexto).
            stats.cache_misses++;
            stats.virtual_time_ns += PIXEL_QUANTUM_NS;  // ciclo del stall
            global_clock_++;

            // Cambio de contexto: cede al siguiente thread activo
            int prev = current_thread;
            switch_to_next_thread();
            if (current_thread != prev) {
                // Penalización real de context switch solo si hubo cambio
                stats.virtual_time_ns += CONTEXT_SWITCH_COST_NS;
                global_clock_++;
            }
        } else {
            // ── COMPUTE ────────────────────────────────────────────────────
            // Renderizar pixel y avanzar al siguiente de este tile
            frame[pixel_idx] = scene.trace(make_ray(x, y, camera_pos_));
            stats.virtual_time_ns += PIXEL_QUANTUM_NS;
            global_clock_++;
            pixel_idx++;

            if (pixel_idx >= task.end) {
                // Tile terminado: ceder al siguiente sin costo extra
                thread_done[thread_id] = true;
                threads_finished++;
                switch_to_next_thread();
            }
        }

        sched_cv.notify_all();
    }
}

std::vector<Vector3> CoarseRenderer::render_frame() {
    // Reiniciar contadores, caches y estado del scheduler
    reset_thread_stats(thread_stats, cache_models);
    for (int i = 0; i < NUM_THREADS; ++i)
        thread_done[i] = false;
    current_thread   = 0;
    threads_finished = 0;
    global_clock_    = 0;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(&CoarseRenderer::render_worker, this, i);
    for (auto& t : threads) t.join();

    // VT total = suma de threads (ejecución serial; stalls ocultos por switch)
    virtual_time_ns_ = sum_virtual_times(thread_stats);

    return frame;
}