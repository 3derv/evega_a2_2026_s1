#ifndef RAY_H
#define RAY_H

#include "Vector3.h"
#include "Constants.h"

// Ray: Representa un rayo de luz en el espacio 3D.
// Almacena origen y dirección normalizada para ray tracing.
struct Ray {
    Vector3 origin;      // Punto donde inicia el rayo
    Vector3 direction;   // Dirección del rayo (normalizada)

    // Constructor: inicializa rayo con origen y dirección.
    // La dirección se normaliza automáticamente.
    Ray(const Vector3& o, const Vector3& d) : origin(o), direction(d.normalize()) {}
};

// make_ray: Convierte coordenadas de píxel a un rayo orientado al espacio NDC [-1, 1].
// Aplica corrección de relación de aspecto (aspect ratio) para que los píxeles
// no aparezcan deformados cuando IMAGE_WIDTH != IMAGE_HEIGHT.
//
// Función libre compartida por todos los renderers (Sequential, FGMT, CGMT) para
// evitar duplicación de la misma lógica de proyección en cada implementación.
//
// Param: x, y — Coordenadas del píxel en espacio de imagen [0..WIDTH, 0..HEIGHT].
// Return: Ray desde el origen con dirección al píxel correspondiente.
inline Ray make_ray(int x, int y) {
    double u      = (2.0 * x / constants::IMAGE_WIDTH)  - 1.0;
    double v      = 1.0 - (2.0 * y / constants::IMAGE_HEIGHT);
    double aspect = static_cast<double>(constants::IMAGE_WIDTH) / constants::IMAGE_HEIGHT;
    Vector3 origin(0, 0, 0);
    Vector3 direction(u * aspect, v, -1);
    return Ray(origin, direction);
}

#endif // RAY_H