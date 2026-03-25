#ifndef GENERIC_RUNNER_H
#define GENERIC_RUNNER_H

#include "IRenderer.h"
#include "Exporter.h"
#include "Metrics.h"
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>

namespace trace {

// GenericRunner: Ejecutor universal de mediciones compatible con cualquier IRenderer.
//
// Responsabilidad:
//   - Ejecutar el renderer N veces para recopilar mediciones de tiempo.
//   - Calcular estadísticas (promedio, min, max, desviación estándar).
//   - Delegar al Exporter para guardar resultados (imagen y CSV).
//   - Funciona con cualquier modelo (secuencial, FGMT, etc.) a través de IRenderer.
// 
// Ventajas:
//   - Desacoplado de implementaciones concretas de renderers.
//   - Permite agregar nuevos modelos sin modificar GenericRunner.
//   - Sigue SOLID: DIP (depende de IRenderer, no de concretos).
struct GenericRunner {
    // Constructor.
    // Param: renderer - Renderer a ejecutar (propiedad trasferida).
    //        runs - Número de ejecuciones a realizar (mínimo 1).
    //        model - Nombre del modelo (usado por Exporter para nombres de archivos).
    GenericRunner(std::unique_ptr<IRenderer> renderer, int runs = 1, const std::string& model = "sequential")
        : renderer_(std::move(renderer)), runs_(std::max(runs, 1)), model_(model), exporter_(model) {}

    // Ejecuta el renderizado N veces y retorna métricas agregadas.
    // Return: Metrics con tiempos de ejecución y estadísticas.
    Metrics run() {
        Metrics metrics;
        metrics.runs = runs_;
        metrics.times.reserve(runs_);
        metrics.virtual_times.reserve(runs_);

        // Ejecutar N veces
        for (int i = 0; i < runs_; ++i) {
            auto start = std::chrono::high_resolution_clock::now();

            // Ejecutar renderizado
            std::vector<Vector3> frame = renderer_->render_frame();

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            metrics.times.push_back(elapsed.count());
            metrics.virtual_times.push_back(renderer_->get_virtual_time_ns());
            metrics.stall_counts.push_back(renderer_->get_total_stalls());

            // Guardar solo la última frame y extraer thread metrics
            if (i == runs_ - 1) {
                exporter_.save_image(frame);
                metrics.thread_metrics = renderer_->get_thread_metrics();
            }
        }
        
        // Calcular estadísticas globales
        metrics.total = std::accumulate(metrics.times.begin(), metrics.times.end(), 0.0);
        metrics.avg = metrics.total / metrics.runs;
        metrics.min = *std::min_element(metrics.times.begin(), metrics.times.end());
        metrics.max = *std::max_element(metrics.times.begin(), metrics.times.end());
        
        // Calcular desviación estándar
        double sum_sq_diff = 0.0;
        for (double t : metrics.times) {
            sum_sq_diff += (t - metrics.avg) * (t - metrics.avg);
        }
        metrics.stddev = std::sqrt(sum_sq_diff / metrics.runs);

        // Calcular tiempo virtual promedio
        long long total_vt = 0LL;
        for (long long vt : metrics.virtual_times) total_vt += vt;
        metrics.virtual_time_ns = total_vt / metrics.runs;

        // Exportar CSV
        exporter_.save_csv(metrics);

        return metrics;
    }

    // Retorna la ruta del archivo imagen generado por este runner.
    const std::string& get_image_file() const { return exporter_.get_image_file(); }

    // Retorna la ruta del archivo CSV generado por este runner.
    const std::string& get_csv_file() const { return exporter_.get_csv_file(); }

private:
    std::unique_ptr<IRenderer> renderer_;  // Renderer (inyectado por DIP)
    int runs_;                             // Número de ejecuciones a realizar
    std::string model_;                    // Nombre del modelo
    Exporter exporter_;                    // Instancia para guardar resultados
};

} // namespace trace

#endif // GENERIC_RUNNER_H
