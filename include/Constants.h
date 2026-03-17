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

} // namespace constants

#endif // CONSTANTS_H