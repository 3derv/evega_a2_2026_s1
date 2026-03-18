#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>

namespace constants {

// Dimensiones de la imagen
inline const int IMAGE_WIDTH = 640;
inline const int IMAGE_HEIGHT = 480;

// Directorios y archivos de salida
inline const std::string RESULTS_DIR = "results";
inline const std::string IMAGE_DIR = "results/image";
inline const std::string IMAGE_FILE = "results/image/frame.ppm";
inline const std::string CSV_FILE = "results/mediciones_secuencial.csv";
inline const std::string GRAPHS_DIR = "results/graficas";

// Parámetros de Fine-Grained Multithreading (FGMT)
inline const int NUM_THREADS = 4;              // 2x2 tiles espaciales
inline const int CACHE_SIZE = 32768;           // 32 KB L1 cache
inline const int NOP_PENALTY_NS = 100;         // nanosegundos por NOP durante miss
inline const int NOPS_PER_STALL = 32;          // NOPs a ejecutar en cada miss

} // namespace constants

#endif // CONSTANTS_H