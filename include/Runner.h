#ifndef RUNNER_H
#define RUNNER_H

#include "Constants.h"
#include "Exporter.h"
#include "Metrics.h"
#include "Renderer.h"
#include <vector>
#include <string>

namespace trace {

// Runner: Ejecutor principal de mediciones de ray tracing secuencial.
// 
// Responsabilidad:
//   - Ejecutar el renderer N veces para recopilar mediciones de tiempo.
//   - Calcular estadísticas (promedio, min, max, desviación estándar).
//   - Delegar al Exporter para guardar resultados (imagen y CSV).
// 
// Notas:
//   - El modelo es secuencial (un único hilo, sin paralelismo).
//   - Útil como línea base para comparación con modelos multihilo futuros.
//   - Los tiempos se miden en segundos con precisión de nanosegundos.
struct Runner {
    // Constructor.
    // Param: runs - Número de ejecuciones a realizar (mínimo 1).
    //              Si se proporciona un valor menor que 1, se ajusta a 1.
    explicit Runner(int runs = 1);

    // Ejecuta el renderizado N veces y retorna métricas agregadas.
    // Return: Metrics con tiempos de ejecución y estadísticas.
    Metrics run();

private:
    int runs_;             // Número de ejecuciones a realizar
    Renderer renderer_;    // Instancia del renderer de ray tracing
    Exporter exporter_;    // Instancia para guardar resultados

    // Ejecuta una única iteración de renderizado y mide su tiempo.
    // Param: frame - Vector donde almacenar el resultado del renderizado.
    // Return: Tiempo de ejecución en segundos.
    double run_once(std::vector<Vector3>& frame);
};

} // namespace trace

#endif // RUNNER_H