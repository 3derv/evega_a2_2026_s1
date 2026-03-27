#ifndef CMP_RENDERER_H
#define CMP_RENDERER_H

#include "IRenderer.h"
#include "Scene.h"
#include "CacheModel.h"
#include "Metrics.h"
#include "Ray.h"
#include "Constants.h"
#include <vector>
#include <thread>

// CMPRenderer: modelo CMP (Chip Multiprocessing) — paralelismo real multinúcleo.
//
// Cada "núcleo" es un hilo del SO corriendo simultáneamente sobre distintos
// núcleos físicos del CPU. NO hay pipeline compartido: cada núcleo tiene su
// propio pipeline, su propia cache L1 y su propio contador de VT.
//
// Diferencias clave con los otros modelos:
//   - Sequential : 1 hilo, stall completo, sin solapamiento.
//   - FGMT/CGMT  : 1 pipeline compartido entre N contextos (time-sliced).
//   - SMT        : 1 core con issue-width=2 (despacha 2 slots/ciclo).
//   - CMP        : N cores independientes en paralelo real (OS threads).
//
// VT(CMP) = max(VT por núcleo) — ya que los núcleos avanzan en paralelo
// el reloj de pared del sistema avanza al ritmo del núcleo más lento.
// Cada núcleo paga su propio CACHE_MISS_PENALTY_NS (sin otro thread que lo oculte).
//
// Speedup teórico (Amdahl, fracción paralela ≈ 1): ~CMP_NUM_CORES × .
// Con 4 núcleos y 4800 píxeles → cada núcleo procesa 1200 px en paralelo.
class CMPRenderer : public IRenderer {
private:
    Scene scene;
    std::vector<Vector3>              frame_;
    std::vector<CacheModel>           cache_models_;
    std::vector<trace::ThreadMetrics> core_stats_;

    // Rango de píxeles asignado a cada núcleo (índice lineal, row-major).
    struct CoreTile {
        int start;   // índice de primer píxel (inclusivo)
        int end;     // índice de último píxel + 1 (exclusivo)
        int core_id;
    };
    std::vector<CoreTile> tiles_;

    long long virtual_time_ns_ = 0LL;
    Vector3   camera_pos_;

    // Worker ejecutado por cada núcleo en un OS thread independiente.
    // Procesa su tile de forma autónoma (Sequential-like) sin coordinación
    // con los otros núcleos: no hay mutex, no hay semáforos, no hay señales.
    void render_core_worker(int core_id);

public:
    // Constructor: divide el frame en CMP_NUM_CORES strips horizontales iguales.
    // Cada core recibe IMAGE_WIDTH * IMAGE_HEIGHT / CMP_NUM_CORES píxeles.
    // CacheModel semilla determinista: base 42 + core_id → reproducibilidad.
    CMPRenderer();

    // Actualiza la posición de cámara antes de render_frame() (órbita elíptica).
    void set_camera_pos(const Vector3& pos) override { camera_pos_ = pos; }

    // render_frame(): lanza CMP_NUM_CORES OS threads, espera a que todos terminen
    // y retorna el frame completo.
    // VT = max(per-core VT): los cores corren en paralelo real.
    std::vector<Vector3> render_frame() override;

    std::string get_model_name() const override { return "cmp"; }
    long long   get_virtual_time_ns() const override { return virtual_time_ns_; }
    int get_total_stalls() const override {
        int total = 0;
        for (const auto& c : core_stats_) total += c.cache_misses;
        return total;
    }
    const std::vector<trace::ThreadMetrics>& get_thread_metrics() const override {
        return core_stats_;
    }
};

#endif // CMP_RENDERER_H
