#ifndef EXPORTER_H
#define EXPORTER_H

#include "Constants.h"
#include "Metrics.h"
#include "Vector3.h"
#include <string>
#include <vector>

namespace trace {

// Exporter: Responsable de guardar resultados (imagen y métricas) a disco.
// 
// Responsabilidad:
//   - Exportar imágenes en formato PPM (Portable Pixmap).
//   - Guardar métricas de medición en formato CSV para análisis posterior.
//   - Manejar creación de directorios si no existen.
//   - Soportar múltiples modelos (secuencial, fgmt, cgmt, etc.)
// 
// Notas:
//   - Las rutas de guardado se definen en Constants.h para centralización.
//   - El formato PPM es simple y portable, legible por cualquier visor de imágenes.
//   - El CSV es procesable por scripts de Python/gnuplot para generar gráficas.
class Exporter {
public:
    // Constructor: inicializar con nombre de modelo para archivos específicos.
    // Param: model - Nombre del modelo (p.ej. "sequential", "fgmt", "cgmt")
    explicit Exporter(const std::string& model = "sequential");

    // Guarda el frame renderizado como imagen PPM.
    // Param: frame - Vector de colores (Vector3: R, G, B en [0, 1]).
    //               Dimensiones: IMAGE_WIDTH * IMAGE_HEIGHT.
    void save_image(const std::vector<Vector3>& frame) const;

    // Guarda las métricas de medición en archivo CSV.
    // Param: metrics - Estructura con tiempos de ejecución y estadísticas.
    //                  El CSV contiene: Ejecucion, Tiempo(s) para plotting.
    void save_csv(const Metrics& metrics) const;

private:
    std::string model_;        // Nombre del modelo
    std::string image_file_;   // Ruta PPM específica del modelo
    std::string csv_file_;     // Ruta CSV específica del modelo
};

} // namespace trace

#endif // EXPORTER_H