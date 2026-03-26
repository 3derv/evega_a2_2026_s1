#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>

namespace constants {

// ============================================================================
// PARÁMETROS DE IMAGEN Y RAY TRACING
// ============================================================================

// Dimensiones de la imagen renderizada (píxeles)
// 160x120 = resolución reducida para medir rendimiento por frame con animación
inline const int IMAGE_WIDTH  = 80;
inline const int IMAGE_HEIGHT = 60;

// ============================================================================
// PARÁMETROS DE ANIMACIÓN
// ============================================================================

// Número de frames a renderizar por ejecución (1° de rotación de cámara por frame)
inline const int NUM_FRAMES = 200;

// ============================================================================
// POSICIONES DE LAS ESFERAS EN LA ESCENA
// ============================================================================

// Esfera 0 — roja, centrada en la escena
inline const double SPHERE0_X = 0.0,  SPHERE0_Y = 0.0, SPHERE0_Z = -5.0, SPHERE0_RADIUS = 1.2;
// Esfera 1 — verde, a la derecha
inline const double SPHERE1_X = 2.0,  SPHERE1_Y = 1.0, SPHERE1_Z = -5.0, SPHERE1_RADIUS = 1.0;
// Esfera 2 — azul, a la izquierda
inline const double SPHERE2_X = -2.0, SPHERE2_Y = -1.0, SPHERE2_Z = -5.0, SPHERE2_RADIUS = 1.0;

// ============================================================================
// CÁMARA Y ÓRBITA ELÍPTICA
// ============================================================================

// Centro de la escena al que apunta la cámara en todo momento
inline const double SCENE_CENTER_X = 0.0;
inline const double SCENE_CENTER_Y = 0.0;
inline const double SCENE_CENTER_Z = -5.0;

// Semi-ejes de la órbita elíptica en el plano XZ.
// A frame 90 (90°) la cámara pasa por (0,0,0) → vista idéntica al baseline.
inline const double CAMERA_ORBIT_RX = 8.0;  // semi-eje mayor (horizontal)
inline const double CAMERA_ORBIT_RZ = 6.0;  // semi-eje menor (profundidad)
inline const double CAMERA_ORBIT_Y  = 0.0;  // altura fija de la cámara

// Directorio donde se vuelcan los 200 PPMs intermedios cuando se usa --gif
inline const std::string GIF_FRAMES_DIR = "tests/gif_utils";

// ============================================================================
// DIRECTORIOS Y ARCHIVOS DE SALIDA
// ============================================================================

inline const std::string RESULTS_DIR  = "results";          // Directorio raíz de resultados
inline const std::string IMAGE_DIR    = "results/image";     // Imágenes PPM renderizadas
inline const std::string GRAPHS_DIR   = "results/graficas";  // Gráficas generadas

// Archivos de imagen por modelo
inline const std::string IMAGE_FILE_SEQUENTIAL = "results/image/frame_secuencial.ppm";
inline const std::string IMAGE_FILE_FGMT       = "results/image/frame_fgmt.ppm";
inline const std::string IMAGE_FILE_CGMT       = "results/image/frame_cgmt.ppm";
inline const std::string IMAGE_FILE_SMT        = "results/image/frame_smt.ppm";
inline const std::string IMAGE_FILE_CMP        = "results/image/frame_cmp.ppm";

// Archivos CSV de mediciones por modelo
inline const std::string CSV_FILE_SEQUENTIAL   = "results/mediciones_secuencial.csv";
inline const std::string CSV_FILE_FGMT         = "results/mediciones_fgmt.csv";
inline const std::string CSV_FILE_CGMT         = "results/mediciones_cgmt.csv";
inline const std::string CSV_FILE_SMT          = "results/mediciones_smt.csv";
inline const std::string CSV_FILE_CMP          = "results/mediciones_cmp.csv";

// ============================================================================
// PARÁMETROS DE CACHE MODELING Y STALLS
// ============================================================================

// Numero de threads en FGMT (Fine-Grained Multithreading)
// Valor 4 => división 2x2 del frame (cada thread procesa 1/4 de píxeles)
inline const int NUM_THREADS = 4;

// Tamaño de cache a simular (bytes)
// La probabilidad base de miss = 64 bytes / CACHE_SIZE (un cache line por píxel).
// Con CACHE_SIZE = 256 → miss_rate ≈ 25% → ~1200 stalls/frame (4800 px)
// Esto produce diferencias de VT visibles entre modelos:
//   Sequential: paga 3200 ns×stall (sin ocultar) → VT ~8.6 ms
//   FGMT:       paga  100 ns×stall (NOP oculto) → speedup ~1.76×
//   CGMT:       paga 1400 ns×stall (slot + switch) → speedup ~1.33×
//   SMT:        stalls COMPLETAMENTE ocultos + W=2 → speedup ~3×
// Con CACHE_SIZE > 4096 el miss rate < 1.6% y las diferencias son < 5%.
inline const int CACHE_SIZE = 256;

// Penalización de latencia por NOP (nanosegundos)
// Cada NOP físico cuesta ~100 ns en CPU moderna
// Este valor es puramente para modelado, no afecta iterations
inline const int NOP_PENALTY_NS = 100;

// Numero de NOPs a ejecutar por cache miss
// En realidad, cada miss causa stall de ~30 ciclos en DRAM
// 32 NOPs ~= aproximación de ese stall
// Mayor valor => más latencia por miss => mayor efecto del multithreading
inline const int NOPS_PER_STALL = 32;

// ============================================================================
// RELOJ VIRTUAL (tiempo simulado por modelo arquitectónico)
// Todos los modelos usan los mismos valores para ser comparables
// ============================================================================

// Tiempo base por pixel: igual en todos los modelos (quantum de ejecución)
inline const long long PIXEL_QUANTUM_NS       = 1000LL;

// Penalizacion completa de un stall: NOPS_PER_STALL x NOP_PENALTY_NS = 3200 ns
// Solo Sequential paga este valor integro (ningun otro thread puede ejecutar).
// FGMT paga PIXEL_QUANTUM_NS (el slot se gasta en un NOP, sin avanzar el pixel).
// CGMT paga 0 ns (cambia de contexto inmediatamente, el slot lo llena otro thread).
// SMT  paga 0 ns (el slot lo llena otro thread del mismo ciclo de emision).
inline const long long CACHE_MISS_PENALTY_NS  = static_cast<long long>(NOPS_PER_STALL) * NOP_PENALTY_NS;

// ============================================================================
// PARÁMETROS DE SMT (Simultaneous Multithreading)
// ============================================================================

// Número de contextos hardware en el núcleo SMT
// Igual a NUM_THREADS para mantener comparabilidad entre modelos
inline const int SMT_NUM_THREADS = 4;

// Issue width W: cuántos pixels puede despachar el pipeline por ciclo.
// W=2 modela un SMT 2-way: el scheduler intenta llenar 2 slots por ciclo
// con threads distintos. En FGMT/CGMT solo 1 slot se llena por ciclo.
// Speedup esperado por Amdahl (fracción paralela ≈ 1): ~W = 2×.
inline const int SMT_ISSUE_WIDTH = 2;

} // namespace constants

#endif // CONSTANTS_H