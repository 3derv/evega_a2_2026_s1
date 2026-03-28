#include "SequentialRenderer.h"
#include "Constants.h"

using namespace constants;

SequentialRenderer::SequentialRenderer() : scene(), cache(CACHE_SIZE) {}

// render_frame: Renderiza todos los píxeles en orden row-major con un solo thread.
//
// Es el único modelo que paga el costo COMPLETO de cada stall (CACHE_MISS_PENALTY_NS)
// porque no existe otro contexto que pueda tomar el pipeline mientras la CPU
// espera el dato de memoria. Esta penalización íntegra es precisamente lo que
// los modelos multithreaded (FGMT, CGMT, SMT) intentan ocultar o amortizar.
std::vector<Vector3> SequentialRenderer::render_frame() {
    std::vector<Vector3> frame(IMAGE_WIDTH * IMAGE_HEIGHT);
    virtual_time_ns_ = 0LL;
    stall_count_ = 0;
    int cycle = 0;

    // Reiniciar cache
    cache.reset();

    logger_.log_header(get_model_name(), 1, 1, PIXEL_QUANTUM_NS, CACHE_MISS_PENALTY_NS);

    // Renderizar con reloj virtual
    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            frame[y * IMAGE_WIDTH + x] = render_pixel(x, y);

            // Quantum: tiempo base por pixel (igual en todos los modelos)
            virtual_time_ns_ += PIXEL_QUANTUM_NS;
            logger_.log_compute(cycle, 0, x, y, PIXEL_QUANTUM_NS);

            // Simular cache behavior
            if (cache.is_cache_miss(x, y)) {
                // Cache miss: stall completo (no hay otro thread que ejecute)
                virtual_time_ns_ += CACHE_MISS_PENALTY_NS;
                ++stall_count_;
                logger_.log_stall(cycle, 0, x, y, CACHE_MISS_PENALTY_NS, "no ctx switch");
            }

            ++cycle;
        }
    }

    return frame;
}
