#include "CMPRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include <algorithm>
#include <thread>

using namespace constants;
using namespace trace;

CMPRenderer::CMPRenderer()
    : frame_(IMAGE_WIDTH * IMAGE_HEIGHT)
{
    const int total_pixels = IMAGE_WIDTH * IMAGE_HEIGHT;
    const int pixels_per_core = total_pixels / CMP_NUM_CORES;

    tiles_.resize(CMP_NUM_CORES);
    for (int i = 0; i < CMP_NUM_CORES; ++i) {
        tiles_[i].start   = i * pixels_per_core;
        tiles_[i].end     = (i == CMP_NUM_CORES - 1) ? total_pixels
                                                       : (i + 1) * pixels_per_core;
        tiles_[i].core_id = i;
    }

    // CacheModel independiente por núcleo: semilla diferenciada para que cada
    // core tenga su propio patrón de misses (cachés L1 físicamente separadas).
    cache_models_.resize(CMP_NUM_CORES);
    for (int i = 0; i < CMP_NUM_CORES; ++i)
        cache_models_[i] = CacheModel(CACHE_SIZE, 42u + static_cast<uint32_t>(i));

    core_stats_.resize(CMP_NUM_CORES);
    for (int i = 0; i < CMP_NUM_CORES; ++i)
        core_stats_[i].thread_id = i;
}

// render_core_worker: pipeline autónomo de un único núcleo CMP.
//
// Modela un núcleo escalar en orden (in-order): despacha 1 pixel/ciclo.
// Cuando hay cache miss, el núcleo ESPERA el dato de memoria — no hay
// otro contexto hardware en este núcleo que pueda ocultar el stall.
// Costo = PIXEL_QUANTUM_NS (quantum base) + CACHE_MISS_PENALTY_NS (latencia DRAM).
//
// No existe coordinación entre núcleos: cero mutexes, cero semáforos.
// El paralelismo real lo provee el SO al mapear cada thread a un core físico.
void CMPRenderer::render_core_worker(int core_id) {
    ThreadMetrics& stats = core_stats_[core_id];
    CacheModel&    cache = cache_models_[core_id];
    const CoreTile& tile = tiles_[core_id];

    for (int idx = tile.start; idx < tile.end; ++idx) {
        const int x = idx % IMAGE_WIDTH;
        const int y = idx / IMAGE_WIDTH;

        // Computar el píxel (ray tracing)
        frame_[idx] = scene.trace(make_ray(x, y, camera_pos_));

        // Quantum base: un ciclo de pipeline productivo
        stats.virtual_time_ns += PIXEL_QUANTUM_NS;

        if (cache.is_cache_miss(x, y)) {
            // Stall del núcleo: no hay otro contexto que tome el pipeline.
            // El núcleo queda idle hasta que el dato regresa de DRAM.
            // Costo idéntico al modelo Sequential pero solo sobre 1/N del frame.
            stats.virtual_time_ns += CACHE_MISS_PENALTY_NS;
            stats.cache_misses++;
        }
    }
}

std::vector<Vector3> CMPRenderer::render_frame() {
    // Resetear estadísticas y caches de todos los núcleos
    for (int i = 0; i < CMP_NUM_CORES; ++i) {
        cache_models_[i].reset();
        core_stats_[i].nops_count      = 0;
        core_stats_[i].nop_time_ns     = 0.0;
        core_stats_[i].cache_misses    = 0;
        core_stats_[i].virtual_time_ns = 0LL;
    }

    // Lanzar CMP_NUM_CORES OS threads: cada uno corre en un core físico distinto.
    // No hay sincronización inter-nucleo durante la ejecución — independencia total.
    std::vector<std::thread> threads;
    threads.reserve(CMP_NUM_CORES);
    for (int i = 0; i < CMP_NUM_CORES; ++i)
        threads.emplace_back(&CMPRenderer::render_core_worker, this, i);

    // Esperar a que todos los núcleos completen su tile
    for (auto& t : threads) t.join();

    // VT(CMP) = max(VT por núcleo).
    // Los núcleos avanzan en paralelo real: el reloj de pared del sistema
    // avanza al ritmo del núcleo más lento (como en hardware real).
    // Contraste: FGMT/CGMT usan suma (pipeline compartido, no paralelo).
    virtual_time_ns_ = 0LL;
    for (const auto& c : core_stats_)
        virtual_time_ns_ = std::max(virtual_time_ns_, c.virtual_time_ns);

    return frame_;
}
