#include "FinegrainedRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include "RendererUtils.h"
#include <cmath>

using namespace constants;
using namespace trace;

FinegrainedRenderer::FinegrainedRenderer()
    : scene(), frame(IMAGE_HEIGHT * IMAGE_WIDTH) {

    tiles.resize(NUM_THREADS);

    // Grilla 2D derivada de NUM_THREADS: ncols = sqrt(N), nrows = N/ncols.
    // Para NUM_THREADS=4 → ncols=2, nrows=2 (misma grilla 2×2 que antes).
    // Si NUM_THREADS cambia a, p. ej., 1 o 2, la grilla se recalcula.
    // Las celdas de la última columna/fila usan IMAGE_WIDTH/IMAGE_HEIGHT como
    // borde derecho/inferior, absorbiendo píxeles sobrantes cuando la imagen
    // no es divisible exactamente entre ncols o nrows.
    const int ncols  = std::max(1, static_cast<int>(std::round(std::sqrt(static_cast<double>(NUM_THREADS)))));
    const int nrows  = NUM_THREADS / ncols;
    const int base_w = IMAGE_WIDTH  / ncols;
    const int base_h = IMAGE_HEIGHT / nrows;

    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            int tid = r * ncols + c;
            int xs  = c * base_w;
            int xe  = (c == ncols - 1) ? IMAGE_WIDTH  : xs + base_w;
            int ys  = r * base_h;
            int ye  = (r == nrows - 1) ? IMAGE_HEIGHT : ys + base_h;
            tiles[tid] = {xs, xe, ys, ye, tid};
        }
    }

    cache_models.resize(NUM_THREADS);
    // Semilla determinista por thread: misma escena → mismo patrón de misses
    // en cada ejecución (reproducibilidad). Se diferencia por thread_id para
    // evitar que todos los tiles compartan el mismo estado inicial del RNG.
    for (int i = 0; i < NUM_THREADS; ++i)
        cache_models[i] = CacheModel(CACHE_SIZE, 42u + static_cast<uint32_t>(i));

    thread_stats.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i)
        thread_stats[i].thread_id = i;
}

// Worker: scheduler FGMT con semáforos por thread (señalización punto a punto).
//
// Cada thread bloquea en sem_wait(&slots_[thread_id]) hasta recibir su turno.
// Al terminar el ciclo, señala directamente al siguiente thread con píxeles
// pendientes (skip-idle), eliminando los spurious wakeups de notify_all().
//
// Terminación: el último thread en completar su tile hace un broadcast
// (sem_post a todos los demás) para que salgan del sem_wait y terminen.
//
// VT: los threads IDLE no consumen slots de pipeline (duermen hasta el broadcast).
void FinegrainedRenderer::render_tile_worker(int thread_id) {
    ThreadMetrics&    stats = thread_stats[thread_id];
    CacheModel&       cache = cache_models[thread_id];
    const ThreadTile& tile  = tiles[thread_id];

    int x = tile.x_start;
    int y = tile.y_start;

    while (true) {
        sem_wait(&slots_[thread_id]);

        // Salida: todos los tiles terminaron (broadcast recibido)
        if (threads_completed_.load(std::memory_order_acquire) == NUM_THREADS) break;

        // Capturar ciclo global antes de procesar: serializado por el semáforo.
        const int cycle = global_cycle_.fetch_add(1, std::memory_order_relaxed);

        // Ciclo de pipeline: este thread tiene píxeles pendientes
        if (cache.is_cache_miss(x, y)) {
            // STALL: el slot se ocupa con un NOP de duración PIXEL_QUANTUM_NS.
            // El slot fue gastado pero no produjo un píxel → el quantum completo
            // se desperdicia. El píxel se reintentará en el próximo turno.
            // Costo = PIXEL_QUANTUM_NS (igual que un compute, pero sin avanzar).
            // Comparar con CGMT: allí el stall cuesta 0 porque el slot es cedido
            // de inmediato a otro thread que sí hace trabajo.
            stats.virtual_time_ns += PIXEL_QUANTUM_NS;
            stats.cache_misses++;
            logger_.log_stall(cycle, thread_id, x, y, PIXEL_QUANTUM_NS, "slot wasted");
        } else {
            // COMPUTE: renderizar pixel y avanzar al siguiente
            frame[y * IMAGE_WIDTH + x] = scene.trace(make_ray(x, y, camera_pos_));
            stats.virtual_time_ns += PIXEL_QUANTUM_NS;
            logger_.log_compute(cycle, thread_id, x, y, PIXEL_QUANTUM_NS);

            x++;
            if (x >= tile.x_end) { x = tile.x_start; y++; }
            if (y >= tile.y_end) {
                logger_.log_done(cycle, thread_id);
                tile_done_[thread_id].store(true, std::memory_order_release);
                int done = threads_completed_.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (done == NUM_THREADS) {
                    // Último en terminar: despertar todos los demás para que salgan
                    for (int i = 0; i < NUM_THREADS; ++i)
                        if (i != thread_id) sem_post(&slots_[i]);
                    break;
                }
                // Este tile terminó — caer al bloque de señalización
                // para pasar el control al siguiente thread activo
            }
        }

        // Señalar al siguiente thread con píxeles pendientes (skip IDLE).
        // Si solo quedo este mismo thread activo, se señala a sí mismo.
        int next    = (thread_id + 1) % NUM_THREADS;
        int checked = 0;
        while (tile_done_[next].load(std::memory_order_acquire) && checked < NUM_THREADS) {
            next = (next + 1) % NUM_THREADS;
            ++checked;
        }
        // checked == NUM_THREADS solo si todos están done — ya salimos arriba
        sem_post(&slots_[next]);
    }
}

std::vector<Vector3> FinegrainedRenderer::render_frame() {
    reset_thread_stats(thread_stats, cache_models);
    virtual_time_ns_   = 0LL;
    threads_completed_.store(0, std::memory_order_relaxed);
    global_cycle_.store(0, std::memory_order_relaxed);
    logger_.log_header("fgmt", NUM_THREADS, 1, PIXEL_QUANTUM_NS, CACHE_MISS_PENALTY_NS);

    // Inicializar semáforos: thread 0 parte listo, el resto bloqueado.
    for (int i = 0; i < NUM_THREADS; ++i) {
        sem_init(&slots_[i], /*pshared=*/0, /*value=*/i == 0 ? 1 : 0);
        tile_done_[i].store(false, std::memory_order_relaxed);
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(&FinegrainedRenderer::render_tile_worker, this, i);
    for (auto& t : threads) t.join();

    for (int i = 0; i < NUM_THREADS; ++i)
        sem_destroy(&slots_[i]);

    // VT total = SUMA de los threads activos (pipeline compartido, no paralelo).
    virtual_time_ns_ = sum_virtual_times(thread_stats);

    return frame;
}
