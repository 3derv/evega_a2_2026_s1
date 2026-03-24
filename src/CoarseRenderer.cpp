#include "CoarseRenderer.h"
#include "Constants.h"
#include "Ray.h"
#include <iostream>
#include <chrono>

using namespace constants;
using namespace trace;

CoarseRenderer::CoarseRenderer()
    : scene(), frame(IMAGE_WIDTH * IMAGE_HEIGHT), current_thread(0), threads_finished(0) {
    
    tasks.resize(NUM_THREADS);
    int total_pixels = IMAGE_WIDTH * IMAGE_HEIGHT;
    int pixels_per_thread = total_pixels / NUM_THREADS;
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        tasks[i].start = i * pixels_per_thread;
        tasks[i].end = (i == NUM_THREADS - 1) 
                        ? total_pixels
                        : (i + 1) * pixels_per_thread;
        tasks[i].thread_id = i;
    }
    
    cache_models.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        cache_models[i] = CacheModel(CACHE_SIZE);
    }
    
    thread_done.resize(NUM_THREADS, false);
    
    thread_stats.resize(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_stats[i].thread_id = i;
        thread_stats[i].nops_count = 0;
        thread_stats[i].nop_time_ns = 0.0;
        thread_stats[i].cache_misses = 0;
    }
}

void CoarseRenderer::render_worker(int thread_id) {
    const Task& task = tasks[thread_id];
    CacheModel& cache = cache_models[thread_id];
    ThreadMetrics& stats = thread_stats[thread_id];
    
    int pixel_idx = task.start;
    
    while (pixel_idx < task.end) {
        // ====================================================================
        // 1. ESPERAR TURNO
        // ====================================================================
        {
            std::unique_lock<std::mutex> lock(sched_mutex);
            
            // Espera indefinidamente hasta que sea su turno
            while (current_thread != thread_id) {
                sched_cv.wait(lock);
            }
        }
        
        // ====================================================================
        // 2. PROCESAR UN ÚNICO PÍXEL
        // ====================================================================
        
        if (pixel_idx < task.end) {
            int y = pixel_idx / IMAGE_WIDTH;
            int x = pixel_idx % IMAGE_WIDTH;
            
            // Renderizar píxel
            double u = (2.0 * x / IMAGE_WIDTH) - 1.0;
            double v = 1.0 - (2.0 * y / IMAGE_HEIGHT);
            double aspect = static_cast<double>(IMAGE_WIDTH) / IMAGE_HEIGHT;
            
            Vector3 origin(0, 0, 0);
            Vector3 direction(u * aspect, v, -1);
            Ray ray(origin, direction);
            Vector3 color = scene.trace(ray);
            frame[pixel_idx] = color;

            // Quantum: tiempo base por pixel en reloj virtual
            stats.virtual_time_ns += PIXEL_QUANTUM_NS;

            // ================================================================
            // 3. REVISAR CACHE MISS (STALL)
            // ================================================================

            if (cache.is_cache_miss(x, y)) {
                // STALL DETECTADO: en CGMT el stall se oculta cediendo al siguiente thread
                stats.cache_misses++;
                // Solo se paga el overhead del cambio de contexto, no la penalización completa
                stats.virtual_time_ns += CONTEXT_SWITCH_COST_NS;

                // ============================================================
                // 4. CAMBIO DE CONTEXTO: CEDE AL SIGUIENTE THREAD NO TERMINADO
                // ============================================================
                {
                    std::unique_lock<std::mutex> lock(sched_mutex);
                    
                    // Encontrar el siguiente thread que NO haya terminado
                    int next_thread = (thread_id + 1) % NUM_THREADS;
                    int attempts = 0;
                    
                    // Saltar threads que ya terminaron
                    while (thread_done[next_thread] && attempts < NUM_THREADS) {
                        next_thread = (next_thread + 1) % NUM_THREADS;
                        attempts++;
                    }
                    
                    // Solo cambiar si encontramos un thread activo
                    if (attempts < NUM_THREADS) {
                        current_thread = next_thread;
                        sched_cv.notify_all();
                    }
                }
            }
            
            pixel_idx++;
        }
    }
    
    // ========================================================================
    // 5. THREAD TERMINÓ
    // ========================================================================
    {
        std::unique_lock<std::mutex> lock(sched_mutex);
        thread_done[thread_id] = true;
        threads_finished++;
        
        // Ceder al siguiente thread ACTIVO (no terminado)
        if (threads_finished < NUM_THREADS) {
            int next_thread = (thread_id + 1) % NUM_THREADS;
            int attempts = 0;
            
            // Saltar threads que ya terminaron
            while (thread_done[next_thread] && attempts < NUM_THREADS) {
                next_thread = (next_thread + 1) % NUM_THREADS;
                attempts++;
            }
            
            // Solo cambiar si encontramos un thread activo
            if (attempts < NUM_THREADS && !thread_done[next_thread]) {
                current_thread = next_thread;
            }
        }
        
        // Despierta a TODOS los threads bloqueados esperando turno
        sched_cv.notify_all();
    }
}

std::vector<Vector3> CoarseRenderer::render_frame() {
    // Reiniciar estado
    for (int i = 0; i < NUM_THREADS; ++i) {
        cache_models[i].reset();
        thread_stats[i].nops_count = 0;
        thread_stats[i].nop_time_ns = 0.0;
        thread_stats[i].cache_misses = 0;
        thread_stats[i].virtual_time_ns = 0LL;
        thread_done[i] = false;
    }
    
    current_thread = 0;
    threads_finished = 0;
    
    // Crear threads
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(&CoarseRenderer::render_worker, this, i);
    }
    
    // Esperar a que todos terminen
    for (auto& t : threads) {
        t.join();
    }

    // Tiempo virtual CGMT = suma de threads (ejecución serial; stalls ocultos por switching)
    virtual_time_ns_ = 0LL;
    for (int i = 0; i < NUM_THREADS; ++i) {
        virtual_time_ns_ += thread_stats[i].virtual_time_ns;
    }

    return frame;
}