#include "SequentialRenderer.h"
#include "Constants.h"

using namespace constants;

SequentialRenderer::SequentialRenderer() : scene(), cache(CACHE_SIZE) {}

std::vector<Vector3> SequentialRenderer::render_frame() {
    std::vector<Vector3> frame(IMAGE_WIDTH * IMAGE_HEIGHT);
    
    // Reiniciar cache
    cache.reset();
    
    // Renderizar con cache modeling y stalls
    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            // Renderizar píxel (sin stalls en el cálculo)
            frame[y * IMAGE_WIDTH + x] = render_pixel(x, y);
            
            // Simular cache behavior
            if (cache.is_cache_miss(x, y)) {
                // Cache miss → ejecutar NOPs de penalización
                volatile int dummy = 0;
                for (int nop_idx = 0; nop_idx < NOPS_PER_STALL; ++nop_idx) {
                    dummy++;  // Simulación de NOP
                    asm volatile("nop");  // NOP arquitectural real (GCC/Clang)
                }
            }
        }
    }
    
    return frame;
}
