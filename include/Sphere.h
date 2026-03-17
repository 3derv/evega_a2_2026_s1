#ifndef SPHERE_H
#define SPHERE_H

#include "Vector3.h"
#include "Ray.h"

// Sphere: Representa una esfera en la escena 3D.
// 
// Responsabilidad:
//   - Almacenar propiedades geométricas (centro, radio).
//   - Almacenar propiedades visuales (color).
//   - Calcular intersección con rayos (ray-sphere intersection).
// 
// Notas matemáticas:
//   - Usa ecuación cuadrática para resolver intersección rayo-esfera.
//   - Solo retorna la intersección más cercana (menor t > 0).
//   - Discriminante < 0 indica sin intersección (rayo no toca esfera).
struct Sphere {
    Vector3 center;    // Centro de la esfera en coordenadas 3D
    double radius;     // Radio de la esfera
    Vector3 color;     // Color RGB (valores en [0, 1])

    // Constructor.
    // Param: c - Centro de la esfera.
    //        r - Radio de la esfera (debe ser > 0).
    //        col - Color RGB en rango [0, 1].
    Sphere(const Vector3& c, double r, const Vector3& col) : center(c), radius(r), color(col) {}

    // Calcula intersección entre un rayo y esta esfera.
    // 
    // Param: ray - Rayo a probar (origen y dirección normalizada).
    //        t - Salida: distancia del rayo a la intersección (si existe).
    // Return: true si hay intersección con t > 0, false en otro caso.
    // 
    // Algoritmo:
    //   1. Resolver ecuación cuadrática: a*t^2 + b*t + c = 0
    //   2. Donde a=1 (dirección normalizada), b/c dependen de posición relativa.
    //   3. Si discriminante >= 0, hay intersección.
    //   4. Retornar la raíz menor positiva (intersección más cercana).
    bool intersect(const Ray& ray, double& t) const {
        Vector3 oc = ray.origin - center;
        double a = ray.direction.dot(ray.direction);
        double b = 2.0 * oc.dot(ray.direction);
        double c = oc.dot(oc) - radius * radius;
        double discriminant = b * b - 4 * a * c;
        if (discriminant < 0) return false;
        t = (-b - sqrt(discriminant)) / (2 * a);
        return t > 0;
    }
};

#endif // SPHERE_H