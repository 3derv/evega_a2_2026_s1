#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>

namespace constants {

// ============================================================================
// PARÁMETROS DE IMAGEN Y RAY TRACING
// ============================================================================

// Dimensiones de la imagen renderizada (píxeles)
// Típico: 640x480 proporciona balance entre claridad y velocidad de renderizado
inline const int IMAGE_WIDTH = 640;
inline const int IMAGE_HEIGHT = 480;

// ============================================================================
// DIRECTORIOS Y ARCHIVOS DE SALIDA
// ============================================================================

inline const std::string RESULTS_DIR = "results";                             // Directorio raíz de resultados
inline const std::string IMAGE_DIR = "results/image";                         // Imágenes PPM renderizadas
inline const std::string IMAGE_FILE = "results/image/frame.ppm";              // Imagen genérica (deprecada)
inline const std::string CSV_FILE = "results/mediciones_secuencial.csv";      // CSV secuencial (deprecado)
inline const std::string GRAPHS_DIR = "results/graficas";                     // Gráficas generadas

// ============================================================================
// PARÁMETROS DE CACHE MODELING Y STALLS
// ============================================================================

// Numero de threads en FGMT (Fine-Grained Multithreading)
// Valor 4 => división 2x2 del frame (cada thread procesa 1/4 de píxeles)
inline const int NUM_THREADS = 4;

// Tamaño de cache a simular (bytes)
// 32 KB = típico L1 cache en CPUs modernas
// Inversamente proporcional a probabilidad de miss: miss_prob = 64 bytes / CACHE_SIZE
// Menor cache => más misses => más NOPs => más tiempo de ejecución
inline const int CACHE_SIZE = 32768;

// Penalización de latencia por NOP (nanosegundos)
// Cada NOP físico cuesta ~100 ns en CPU moderna
// Este valor es puramente para modelado, no afecta iterations
inline const int NOP_PENALTY_NS = 100;

// Numero de NOPs a ejecutar por cache miss
// En realidad, cada miss causa stall de ~30 ciclos en DRAM
// 32 NOPs ~= aproximación de ese stall
// Mayor valor => más latencia por miss => mayor efecto del multithreading
inline const int NOPS_PER_STALL = 32;

} // namespace constants

#endif // CONSTANTS_H