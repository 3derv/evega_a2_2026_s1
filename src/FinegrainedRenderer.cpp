#include "FinegrainedRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include "RendererUtils.h"

using namespace constants;
using namespace trace;

FinegrainedRenderer::FinegrainedRenderer()
    : scene(), frame(IMAGE_HEIGHT * IMAGE_WIDTH) {

    tiles.resize(NUM_THREADS);
    int tile_width  = IMAGE_WIDTH  / 2;
    int tile_height = IMAGE_HEIGHT / 2;

    tiles[0] = {0,          tile_width,  0,           tile_height,  0};
    tiles[1] = {tile_width, IMAGE_WIDTH, 0,           tile_height,  1};
    tiles[2] = {0,          tile_width,  tile_height, IMAGE_HEIGHT, 2};
    tiles[3] = {tile_width, IMAGE_WIDTH, tile_height, IMAGE_HEIGHT, 3};

    cache_models.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i)
        cache_models[i] = CacheModel(CACHE_SIZE);

    thread_stats.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i)
        thread_stats[i].thread_id = i;
}

// Worker: ejecuta el scheduler FGMT con reloj global explícito.
//
// Mecanismo (idéntico al código de referencia en C):
//   - El thread bloquea en clock_tick_ hasta que global_clock_ % NUM_THREADS == thread_id.
//   - Al despertar, ejecuta UN ciclo de pipeline:
//       COMPUTE   — renderiza el pixel actual y avanza al siguiente.
//       NOP/STALL — consume el slot pero el pixel se reintenta el próximo turno.
//       IDLE      — el tile ya terminó; sigue participando para no bloquear el clock.
//   - Incrementa global_clock_ y hace notify_all() para despertar al siguiente thread.
//
// La diferencia clave con CGMT: en FGMT la rotación es SIEMPRE cada ciclo,
// no solo en stalls. El costo del stall completo (3200 ns) queda oculto porque
// mientras este thread espera memoria otro contexto ocupa el pipeline.
void FinegrainedRenderer::render_tile_worker(int thread_id) {
    ThreadMetrics&    stats = thread_stats[thread_id];
    CacheModel&       cache = cache_models[thread_id];
    const ThreadTile& tile  = tiles[thread_id];

    int  x            = tile.x_start;
    int  y            = tile.y_start;
    bool my_work_done = false;

    while (true) {
        std::unique_lock<std::mutex> lock(pipeline_mutex_);

        // Salida anticipada: todos los threads completaron su tile
        if (threads_completed_ == NUM_THREADS) break;

        // FGMT round-robin: esperar el ciclo de este thread.
        // Condición de despertar: es mi turno O ya todos terminaron.
        clock_tick_.wait(lock, [&] {
            return (global_clock_ % NUM_THREADS) == thread_id
                || threads_completed_ == NUM_THREADS;
        });

        if (threads_completed_ == NUM_THREADS) break;

        if (!my_work_done) {
            if (cache.is_cache_miss(x, y)) {
                // STALL/NOP: el slot se consume pero el pixel no avanza.
                // El stall completo (3200 ns) queda oculto — otro contexto
                // ejecuta mientras este espera la memoria.
                stats.virtual_time_ns += NOP_PENALTY_NS;
                stats.nops_count++;
                stats.cache_misses++;
            } else {
                // COMPUTE: renderizar pixel y avanzar al siguiente
                frame[y * IMAGE_WIDTH + x] = scene.trace(make_ray(x, y));
                stats.virtual_time_ns += PIXEL_QUANTUM_NS;

                x++;
                if (x >= tile.x_end) { x = tile.x_start; y++; }
                if (y >= tile.y_end) {
                    my_work_done = true;
                    threads_completed_++;  // atómico bajo el mutex
                }
            }
        } else {
            // IDLE: tile terminado, pero el thread sigue en el pipeline
            // para que los demás contextos obtengan su turno (no bloquear el clock).
            stats.virtual_time_ns += NOP_PENALTY_NS;
        }

        // Avanzar el reloj global y despertar al siguiente thread
        global_clock_++;
        clock_tick_.notify_all();
    }
}

std::vector<Vector3> FinegrainedRenderer::render_frame() {
    reset_thread_stats(thread_stats, cache_models);
    virtual_time_ns_   = 0LL;
    global_clock_      = 0;
    threads_completed_ = 0;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(&FinegrainedRenderer::render_tile_worker, this, i);
    for (auto& t : threads) t.join();

    // VT total = SUMA de los cuatro contexts (pipeline compartido, no paralelo).
    // Cada ciclo solo 1 context ejecuta; la suma equivale a los ciclos totales
    // multiplicados por el costo promedio por ciclo.
    virtual_time_ns_ = sum_virtual_times(thread_stats);

    return frame;
}
