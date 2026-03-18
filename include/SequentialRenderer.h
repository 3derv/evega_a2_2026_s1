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
    // Constructor: inicializa la escena y cache model
    // El cache se configura con valor de Constants::CACHE_SIZE
    SequentialRenderer();

    // render_pixel(): Renderiza un píxel individual sin stalls.
    // Param: x, y - Coordenadas del píxel (0..IMAGE_WIDTH, 0..IMAGE_HEIGHT)
    // Return: Color resultante (Vector3 R, G, B en [0, 1])
    // Nota: Este método realiza el cálculo de ray tracing pero NO ejecuta NOPs
    //       Los NOPs se ejecutan en render_frame() basándose en cache misses
    Vector3 render_pixel(int x, int y) const {
        double u = (2.0 * x / 640.0) - 1.0;
        double v = 1.0 - (2.0 * y / 480.0);
        double aspect = 640.0 / 480.0;

        Vector3 origin(0, 0, 0);
        Vector3 direction(u * aspect, v, -1);
        Ray ray(origin, direction);

        return scene.trace(ray);
    }

    // render_frame(): Implementación de IRenderer. Renderiza frame secuencialmente.
    // Responsabilidad:
    //   1. Resetear cache model
    //   2. Iterar píxeles en orden row-major (fila por fila, columna por columna)
    //   3. Para cada píxel:
    //      - Llamar render_pixel() para calcular color
    //      - Consultar cache.is_cache_miss(x, y)
    //      - Si hay miss: ejecutar NOPS_PER_STALL operaciones NOP
    //   4. Retornar frame buffer completo
    // Return: Vector<Vector3> con dimensiones IMAGE_WIDTH × IMAGE_HEIGHT
    // Performance: ~12ms con cache misses (vs ~5-6ms en FGMT con 4 threads)
    std::vector<Vector3> render_frame() override;

    // get_model_name(): Retorna identificador del modelo para logging/CSV
    // Return: String "sequential"
    std::string get_model_name() const override { return "sequential"; }
};

#endif // SEQUENTIAL_RENDERER_H
