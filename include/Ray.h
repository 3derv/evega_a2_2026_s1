#ifndef RAY_H
#define RAY_H

#include "Vector3.h"

// Ray: Representa un rayo de luz en el espacio 3D.
// Almacena origen y dirección normalizada para ray tracing.
struct Ray {
    Vector3 origin;      // Punto donde inicia el rayo
    Vector3 direction;   // Dirección del rayo (normalizada)

    // Constructor: inicializa rayo con origen y dirección.
    // La dirección se normaliza automáticamente.
    Ray(const Vector3& o, const Vector3& d) : origin(o), direction(d.normalize()) {}
};

#endif // RAY_H