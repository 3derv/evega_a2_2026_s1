#include "SequentialRenderer.h"
#include "Constants.h"

using namespace constants;

SequentialRenderer::SequentialRenderer() : scene(), cache(CACHE_SIZE) {}

std::vector<Vector3> SequentialRenderer::render_frame() {
    std::vector<Vector3> frame(IMAGE_WIDTH * IMAGE_HEIGHT);
    virtual_time_ns_ = 0LL;
    stall_count_ = 0;

    // Reiniciar cache
    cache.reset();

    // Renderizar con reloj virtual
    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            frame[y * IMAGE_WIDTH + x] = render_pixel(x, y);

            // Quantum: tiempo base por pixel (igual en todos los modelos)
            virtual_time_ns_ += PIXEL_QUANTUM_NS;

            // Simular cache behavior
            if (cache.is_cache_miss(x, y)) {
                // Cache miss: stall completo (no hay otro thread que ejecute)
                virtual_time_ns_ += CACHE_MISS_PENALTY_NS;
                ++stall_count_;
            }
        }
    }

    return frame;
}
