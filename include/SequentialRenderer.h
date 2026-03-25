#ifndef SEQUENTIAL_RENDERER_H
#define SEQUENTIAL_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "Ray.h"
#include "CacheModel.h"
#include "Constants.h"
#include <vector>

// SequentialRenderer: modelo de referencia sin multithreading (baseline).
//
// Renderiza todos los pixels en un solo hilo, fila por fila (row-major).
// Es el unico modelo que paga el costo COMPLETO de cada stall:
//   CACHE_MISS_PENALTY_NS = NOPS_PER_STALL x NOP_PENALTY_NS = 3200 ns
// porque no hay otro thread que pueda ejecutar mientras espera la memoria.
//
// Se usa como linea base para calcular el speedup de FGMT y CGMT.
class SequentialRenderer : public IRenderer {
private:
    Scene scene;
    CacheModel cache;                  // Una unica instancia de cache (hilo unico)
    long long virtual_time_ns_ = 0LL; // Tiempo de reloj virtual del ultimo render_frame()
    int stall_count_ = 0;             // Cache misses en el ultimo render_frame()
    
public:
    SequentialRenderer();

    // Proyecta el pixel (x, y) al espacio NDC y traza el rayo resultante.
    // Delega la proyeccion a make_ray() (Ray.h) para evitar duplicacion.
    Vector3 render_pixel(int x, int y) const {
        return scene.trace(make_ray(x, y));
    }

    std::vector<Vector3> render_frame() override;
    std::string get_model_name() const override { return "sequential"; }
    long long get_virtual_time_ns() const override { return virtual_time_ns_; }
    int get_total_stalls() const override { return stall_count_; }
};

#endif // SEQUENTIAL_RENDERER_H
