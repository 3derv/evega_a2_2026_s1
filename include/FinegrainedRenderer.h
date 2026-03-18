#ifndef FINEGRAINED_RENDERER_H
#define FINEGRAINED_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include <vector>
#include <thread>
#include <chrono>

// FinegrainedRenderer: Renderizador FGMT con 4 threads independientes.
// Hereda de IRenderer para permitir polimorfismo.
class FinegrainedRenderer : public IRenderer {
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
    // Param: thread_id - Identificador del thread (0..NUM_THREADS-1)
    // Procesa el tile asignado de píxeles, simulando cache behavior y ejecutando NOPs
    void render_tile_worker(int thread_id);

public:
    // Constructor: inicializar renderer, crear tiles (2x2), cache models por thread
    // Divide el frame en 4 tiles iguales (NUM_THREADS=4), uno por thread
    // Inicializa CacheModel para cada thread con parámetros de Constants.h
    FinegrainedRenderer();

    // render_frame(): Implementación de IRenderer. Renderiza frame con 4 threads paralelos.
    // Responsabilidad:
    //   1. Resetear cache models y estadísticas de threads
    //   2. Crear 4 threads (uno por tile)
    //   3. Cada thread procesa su tile con cache modeling y NOPs
    //   4. Sincronizar (join) al finalizar todos los threads
    //   5. Retornar frame buffer con píxeles finales
    // Return: Vector<Vector3> con dimensiones IMAGE_WIDTH × IMAGE_HEIGHT
    std::vector<Vector3> render_frame() override;

    // get_model_name(): Retorna identificador del modelo para logging/CSV
    // Return: String "fgmt"
    std::string get_model_name() const override { return "fgmt"; }

    // get_thread_metrics(): Obtener estadísticas de threads de la última ejecución
    // Incluye: nops_count, nop_time_ns, cache_misses por cada thread
    // Return: Vector<ThreadMetrics> con una entrada por thread
    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const { return thread_stats; }

    // get_frame(): Acceder al frame procesado (para debugging o exportación manual)
    // Return: Vector<Vector3> con píxeles completamente renderizados
    const std::vector<Vector3>& get_frame() const { return frame; }
};

#endif // FINEGRAINED_RENDERER_H
