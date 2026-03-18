#ifndef SEQUENTIAL_RENDERER_H
#define SEQUENTIAL_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "Ray.h"
#include "CacheModel.h"
#include <vector>

// SequentialRenderer: Renderizador secuencial de ray tracing con cache modeling.
//
// Responsabilidad:
//   - Implementar renderizado secuencial (un único hilo).
//   - Procesar píxeles secuencialmente sin paralelismo.
//   - Simular cache hits/misses y stalls (NOPs) durante renderizado.
//   - Servir como línea base con realismo para comparación con otros modelos.
//
// Hereda de IRenderer para permitir polimorfismo y Factory Pattern.
class SequentialRenderer : public IRenderer {
private:
    Scene scene;
    CacheModel cache;  // Simulador de cache para un único thread
    
public:
    // Constructor: inicializa la escena y cache model.
    SequentialRenderer();

    // Renderiza un píxel individual (sin stalls).
    Vector3 render_pixel(int x, int y) const {
        double u = (2.0 * x / 640.0) - 1.0;
        double v = 1.0 - (2.0 * y / 480.0);
        double aspect = 640.0 / 480.0;

        Vector3 origin(0, 0, 0);
        Vector3 direction(u * aspect, v, -1);
        Ray ray(origin, direction);

        return scene.trace(ray);
    }

    // Implementación de IRenderer::render_frame().
    std::vector<Vector3> render_frame() override;

    // Implementación de IRenderer::get_model_name().
    std::string get_model_name() const override { return "sequential"; }
};

#endif // SEQUENTIAL_RENDERER_H
