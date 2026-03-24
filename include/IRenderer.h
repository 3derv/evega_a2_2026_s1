#ifndef IRENDERER_H
#define IRENDERER_H

#include <vector>
#include <string>
#include "Vector3.h"
#include "Metrics.h"

// IRenderer: Interfaz abstracta para renderizadores.
//
// Define el contrato que todos los modelos de ejecución deben cumplir.
// Permite abstraer detalles de implementación (secuencial, FGMT, CGMT, etc.)
// y habilita el uso de Factory Pattern para creación dinámica de renderers.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Renderiza un frame completo y retorna el buffer de píxeles.
    // Return: Vector de colores (Vector3) con dimensiones IMAGE_WIDTH × IMAGE_HEIGHT.
    virtual std::vector<Vector3> render_frame() = 0;

    // Retorna estadísticas por thread (vacío para modelos sin threads).
    virtual const std::vector<trace::ThreadMetrics>& get_thread_metrics() const {
        static const std::vector<trace::ThreadMetrics> empty;
        return empty;
    }

    // Retorna el tiempo virtual simulado del último render_frame() (nanosegundos).
    // Cada modelo calcula este valor según su arquitectura:
    //   - Sequential: Σ(PIXEL_QUANTUM + CACHE_MISS_PENALTY × misses)
    //   - FGMT:       Σ(virtual_time por thread)    [1 pipeline, round-robin + stalls diferidos]
    //   - CGMT:       Σ(virtual_time por thread)    [ejecución serial + stalls ocultos]
    virtual long long get_virtual_time_ns() const { return 0LL; }

    // Retorna el total de stalls (cache misses) del último render_frame().
    virtual int get_total_stalls() const { return 0; }

    // Retorna el nombre del modelo para debugging/logging.
    virtual std::string get_model_name() const = 0;
};

#endif // IRENDERER_H
