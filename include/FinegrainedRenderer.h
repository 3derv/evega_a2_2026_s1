#ifndef FINEGRAINED_RENDERER_H
#define FINEGRAINED_RENDERER_H

#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include <vector>
#include <thread>
#include <chrono>

// FinegrainedRenderer: Renderizador FGMT con 4 threads independientes.
// 
// Estrategia de paralelización: División espacial 2x2
//   - Frame 640x480 → 4 tiles de 320x240 píxeles
//   - 1 thread por tile (localidad espacial mejorada)
//   - Quantum = procesar 1 píxel (ray-sphere intersection)
//   - Cache stalls simulados con NOPs
//
// Modelo de stalls:
//   - Cada thread mantiene su propio CacheModel
//   - Probabilidad de miss basada en tamaño de cache y localidad
//   - En miss: ejecutar NOPS_PER_STALL NOPs (latencia = NOP_PENALTY_NS * NOPS_PER_STALL)
//   - Countear NOPs totales por thread para estadísticas
//
// Sincronización:
//   - Threads independientes procesando tiles sin lock data
//   - Barrier al final para reunir métricas
//   - Cada thread actualiza su ThreadMetrics localmente (sin contención)
class FinegrainedRenderer {
private:
    Scene scene;                                  // Escena común (read-only)
    std::vector<Vector3> frame;                   // Frame buffer compartido (1D: ancho * alto)
    std::vector<CacheModel> cache_models;         // 1 CacheModel por thread
    std::vector<trace::ThreadMetrics> thread_stats; // Estadísticas per-thread
    
    // Rango de píxeles para cada thread (índices lineares)
    struct ThreadTile {
        int x_start, x_end;  // Rango X
        int y_start, y_end;  // Rango Y
        int thread_id;
    };
    std::vector<ThreadTile> tiles;

    // Función worker ejecutada por cada thread
    void render_tile_worker(int thread_id);

public:
    // Constructor: inicializar renderer, crear tiles, cache models
    FinegrainedRenderer();

    // render_frame(): Ejecutar renderizado paralelo con 4 threads
    // Retorna vector de ThreadMetrics con NOPs contados por thread
    std::vector<trace::ThreadMetrics> render_frame();

    // get_frame(): Acceder al frame procesado
    const std::vector<Vector3>& get_frame() const { return frame; }
};

#endif // FINEGRAINED_RENDERER_H
