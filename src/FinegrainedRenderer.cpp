#include "FinegrainedRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include <iostream>

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
    pixel_queues.resize(NUM_THREADS);
    thread_done_flags.resize(NUM_THREADS, false);

    for (int i = 0; i < NUM_THREADS; ++i)
        thread_stats[i].thread_id = i;
}

// Worker: ejecuta la cola de píxeles del tile con scheduler round-robin.
// FGMT: cede el pipeline tras CADA quantum (a diferencia de CGMT que solo cede en stall).
// Stall → pixel diferido como prefetched; pipeline cedido al siguiente contexto.
void FinegrainedRenderer::render_tile_worker(int thread_id) {
    ThreadMetrics& stats = thread_stats[thread_id];
    CacheModel&    cache = cache_models[thread_id];
    auto&          queue = pixel_queues[thread_id];

    while (true) {
        // ────────────────────────────────────────────────────────────────
        // 1. ESPERAR TURNO EN EL PIPELINE (round-robin)
        // ────────────────────────────────────────────────────────────────
        {
            std::unique_lock<std::mutex> lock(pipeline_mutex);
            while (pipeline_owner != thread_id)
                pipeline_cv.wait(lock);
        }

        // ────────────────────────────────────────────────────────────────
        // 2. ¿QUEDAN PÍXELES? SI NO, LIBERAR PIPELINE Y TERMINAR
        // ────────────────────────────────────────────────────────────────
        if (queue.empty()) {
            std::unique_lock<std::mutex> lock(pipeline_mutex);
            thread_done_flags[thread_id] = true;
            active_threads_count--;
            if (active_threads_count > 0) {
                int next     = (thread_id + 1) % NUM_THREADS;
                int attempts = 0;
                while (thread_done_flags[next] && attempts < NUM_THREADS) {
                    next = (next + 1) % NUM_THREADS;
                    ++attempts;
                }
                pipeline_owner = next;
            }
            pipeline_cv.notify_all();
            return;
        }

        // ────────────────────────────────────────────────────────────────
        // 3. PROCESAR SIGUIENTE PIXEL
        // ────────────────────────────────────────────────────────────────
        PixelTask task = queue.front();
        queue.pop_front();

        if (!task.prefetched && cache.is_cache_miss(task.x, task.y)) {
            // STALL: 1 NOP (ciclo de pipeline desperdiciado) + diferir pixel.
            // El pixel se reencola como "prefetched": en su próximo turno se
            // calculará sin reintentar el check de cache (dato ya en memoria).
            stats.virtual_time_ns += NOP_PENALTY_NS;
            stats.nops_count++;
            stats.cache_misses++;  // = stall_count por thread
            queue.push_back({task.x, task.y, true});
        } else {
            // HIT o prefetched: calcular pixel normalmente
            double u      = (2.0 * task.x / IMAGE_WIDTH)  - 1.0;
            double v      = 1.0 - (2.0 * task.y / IMAGE_HEIGHT);
            double aspect = (double)IMAGE_WIDTH / IMAGE_HEIGHT;
            Vector3 origin(0, 0, 0);
            Vector3 direction(u * aspect, v, -1);
            Ray ray(origin, direction);
            frame[task.y * IMAGE_WIDTH + task.x] = scene.trace(ray);
            stats.virtual_time_ns += PIXEL_QUANTUM_NS;
        }

        // ────────────────────────────────────────────────────────────────
        // 4. YIELD OBLIGATORIO: FGMT cede el pipeline tras cada quantum.
        //    Si solo queda este thread activo, next == thread_id → continúa.
        // ────────────────────────────────────────────────────────────────
        {
            std::unique_lock<std::mutex> lock(pipeline_mutex);
            int next     = (thread_id + 1) % NUM_THREADS;
            int attempts = 0;
            while (thread_done_flags[next] && attempts < NUM_THREADS) {
                next = (next + 1) % NUM_THREADS;
                ++attempts;
            }
            pipeline_owner = next;
            pipeline_cv.notify_all();
        }
    }
}

std::vector<Vector3> FinegrainedRenderer::render_frame() {
    for (int i = 0; i < NUM_THREADS; ++i) {
        cache_models[i].reset();
        thread_stats[i].nops_count      = 0;
        thread_stats[i].nop_time_ns     = 0.0;
        thread_stats[i].cache_misses    = 0;
        thread_stats[i].virtual_time_ns = 0LL;
        thread_done_flags[i]            = false;
        pixel_queues[i].clear();

        // Poblar cola de píxeles del tile asignado
        const ThreadTile& tile = tiles[i];
        for (int y = tile.y_start; y < tile.y_end; ++y)
            for (int x = tile.x_start; x < tile.x_end; ++x)
                pixel_queues[i].push_back({x, y, false});
    }

    pipeline_owner       = 0;
    active_threads_count = NUM_THREADS;
    virtual_time_ns_     = 0LL;

    // Lanzar threads — cada uno espera turno en el pipeline round-robin
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads.emplace_back(&FinegrainedRenderer::render_tile_worker, this, i);
    for (auto& t : threads) t.join();

    // Tiempo virtual = SUMA (1 pipeline compartido: todos los quanta son seriales)
    for (int i = 0; i < NUM_THREADS; ++i)
        virtual_time_ns_ += thread_stats[i].virtual_time_ns;

    return frame;
}
