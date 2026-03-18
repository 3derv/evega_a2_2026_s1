#include "FinegrainedRenderer.h"
#include "Constants.h"
#include <iostream>
#include <barrier>

using namespace constants;
using namespace trace;

// Constructor: Inicializar tiles y cache models
FinegrainedRenderer::FinegrainedRenderer() 
    : scene(), frame(IMAGE_HEIGHT * IMAGE_WIDTH) {
    
    // Inicializar 4 tiles (división 2x2)
    tiles.resize(NUM_THREADS);
    int tile_width = IMAGE_WIDTH / 2;
    int tile_height = IMAGE_HEIGHT / 2;
    
    // Tile 0: esquina superior-izquierda
    tiles[0] = {0, tile_width, 0, tile_height, 0};
    
    // Tile 1: esquina superior-derecha
    tiles[1] = {tile_width, IMAGE_WIDTH, 0, tile_height, 1};
    
    // Tile 2: esquina inferior-izquierda
    tiles[2] = {0, tile_width, tile_height, IMAGE_HEIGHT, 2};
    
    // Tile 3: esquina inferior-derecha
    tiles[3] = {tile_width, IMAGE_WIDTH, tile_height, IMAGE_HEIGHT, 3};
    
    // Inicializar CacheModel para cada thread
    cache_models.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        cache_models[i] = CacheModel(CACHE_SIZE);
    }
    
    // Inicializar estadísticas de threads
    thread_stats.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_stats[i].thread_id = i;
        thread_stats[i].nops_count = 0;
        thread_stats[i].nop_time_ns = 0.0;
        thread_stats[i].cache_misses = 0;
    }
}

// Función worker: procesar un tile de píxeles
void FinegrainedRenderer::render_tile_worker(int thread_id) {
    const ThreadTile& tile = tiles[thread_id];
    CacheModel& cache = cache_models[thread_id];
    ThreadMetrics& stats = thread_stats[thread_id];
    
    // Iterar píxeles en el tile (fila por fila, columna por columna = localidad mejora)
    for (int y = tile.y_start; y < tile.y_end; ++y) {
        for (int x = tile.x_start; x < tile.x_end; ++x) {
            // Quantum: procesar este píxel
            
            // 1. Normalizar coordenadas a [-1, 1]
            double u = (2.0 * x - IMAGE_WIDTH) / IMAGE_HEIGHT;
            double v = (2.0 * y - IMAGE_HEIGHT) / IMAGE_HEIGHT;
            
            // 2. Crear rayo desde cámara
            Vector3 ray_dir = Vector3(u, v, 1.0).normalize();
            Ray ray(Vector3(0, 0, -5), ray_dir);
            
            // 3. Trazar rayo en escena
            Vector3 color = scene.trace(ray);
            
            // 4. Simular cache behavior
            if (cache.is_cache_miss(x, y)) {
                // Cache miss → ejecutar NOPs de penalización
                stats.cache_misses++;
                stats.nops_count += NOPS_PER_STALL;
                stats.nop_time_ns += (long long)NOPS_PER_STALL * NOP_PENALTY_NS;
                
                // Simular latencia: busy-wait con NOPs (inline asm o loop)
                volatile int dummy = 0;
                for (int nop_idx = 0; nop_idx < NOPS_PER_STALL; ++nop_idx) {
                    dummy++;  // Simulación de NOP
                    asm volatile("nop");  // NOP arquitectural real (GCC/Clang)
                }
            }
            
            // 5. Guardar píxel en frame buffer
            frame[y * IMAGE_WIDTH + x] = color;
        }
    }
}

// render_frame(): Ejecutar renderizado con threads paralelos
std::vector<ThreadMetrics> FinegrainedRenderer::render_frame() {
    // Reiniciar cache models y estadísticas
    for (int i = 0; i < NUM_THREADS; ++i) {
        cache_models[i].reset();
        thread_stats[i].nops_count = 0;
        thread_stats[i].nop_time_ns = 0.0;
        thread_stats[i].cache_misses = 0;
    }
    
    // Crear y lanzar threads
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(&FinegrainedRenderer::render_tile_worker, this, i);
    }
    
    // Esperar a que todos los threads terminen (barrier implícito)
    for (auto& t : threads) {
        t.join();
    }
    
    // Retornar estadísticas de todos los threads
    return thread_stats;
}
