#ifndef GENERIC_RUNNER_H
#define GENERIC_RUNNER_H

#include "IRenderer.h"
#include "Exporter.h"
#include "Metrics.h"
#include "CameraPath.h"
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>

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

    // Activa el volcado de cada frame individual como PPM en el directorio dado.
    // Cuando está habilitado, run() guarda frame_0000.ppm ... frame_0199.ppm
    // en frames_dir antes de generar el GIF con FFmpeg.
    void set_frames_dir(const std::string& dir) { frames_dir_ = dir; }

    // Ejecuta la animación de NUM_FRAMES frames y retorna métricas agregadas.
    // Por cada frame: avanza la cámara 1° en la órbita elíptica, renderiza y
    // graba el tiempo en el CSV. El scheduling interno de cada modelo no cambia.
    // Return: Metrics con tiempo por frame y estadísticas agregadas.
    Metrics run() {
        Metrics metrics;
        metrics.runs = constants::NUM_FRAMES;
        metrics.times.reserve(constants::NUM_FRAMES);
        metrics.virtual_times.reserve(constants::NUM_FRAMES);

        std::vector<Vector3> last_frame;

        // Iterar sobre los NUM_FRAMES frames de la animación
        for (int frame = 0; frame < constants::NUM_FRAMES; ++frame) {
            // Avanzar cámara: 1° por frame sobre la órbita elíptica
            renderer_->set_camera_pos(camera_pos_for_frame(frame));

            auto start = std::chrono::high_resolution_clock::now();
            last_frame = renderer_->render_frame();
            auto end   = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double> elapsed = end - start;
            metrics.times.push_back(elapsed.count());
            metrics.virtual_times.push_back(renderer_->get_virtual_time_ns());
            metrics.stall_counts.push_back(renderer_->get_total_stalls());

            // Guardar frame individual si se pidió exportación de GIF
            if (!frames_dir_.empty()) {
                save_ppm_frame(last_frame, frame);
            }
        }

        // Guardar el último frame como imagen de referencia
        exporter_.save_image(last_frame);
        metrics.thread_metrics = renderer_->get_thread_metrics();
        
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
    std::string frames_dir_;               // Directorio para frames intermedios (vacío = deshabilitado)

    // Guarda un único frame como frame_NNNN.ppm en frames_dir_.
    void save_ppm_frame(const std::vector<Vector3>& frame, int idx) const {
        std::filesystem::create_directories(frames_dir_);
        std::ostringstream name;
        name << frames_dir_ << "/frame_" << std::setw(4) << std::setfill('0') << idx << ".ppm";
        std::ofstream f(name.str());
        if (!f) return;
        f << "P3\n" << constants::IMAGE_WIDTH << " " << constants::IMAGE_HEIGHT << "\n255\n";
        for (const auto& c : frame) {
            int r = static_cast<int>(std::clamp(c.x * 255, 0.0, 255.0));
            int g = static_cast<int>(std::clamp(c.y * 255, 0.0, 255.0));
            int b = static_cast<int>(std::clamp(c.z * 255, 0.0, 255.0));
            f << r << " " << g << " " << b << "\n";
        }
    }
};

} // namespace trace

#endif // GENERIC_RUNNER_H
