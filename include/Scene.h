#ifndef SCENE_H
#define SCENE_H

#include "Sphere.h"
#include "Ray.h"
#include <vector>

struct Scene {
    std::vector<Sphere> spheres;

    Scene() {
        // Añadir algunas esferas de ejemplo
        spheres.emplace_back(Vector3(0, 0, -5), 1.0, Vector3(1, 0, 0)); // Roja
        spheres.emplace_back(Vector3(2, 0, -5), 1.0, Vector3(0, 1, 0)); // Verde
        spheres.emplace_back(Vector3(-2, 0, -5), 1.0, Vector3(0, 0, 1)); // Azul
    }

    Vector3 trace(const Ray& ray) const {
        double t_min = INFINITY;
        Vector3 color(0, 0, 0); // Fondo negro
        for (const auto& sphere : spheres) {
            double t;
            if (sphere.intersect(ray, t) && t < t_min) {
                t_min = t;
                color = sphere.color;
            }
        }
        return color;
    }
};

#endif // SCENE_H