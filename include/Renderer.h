#ifndef RENDERER_H
#define RENDERER_H

#include "Constants.h"
#include "Scene.h"
#include "Vector3.h"
#include <vector>

struct Renderer {
    Scene scene;  // Escena con objetos a renderizar

    // Renderiza un píxel individual calculando un rayo y trazándolo.
    // Param: x, y - Coordenadas del píxel en la imagen (0..WIDTH, 0..HEIGHT).
    // Return: Color resultante (Vector3 con R, G, B en [0, 1]).
    Vector3 render_pixel(int x, int y) const {
        // Coordenadas normalizadas (-1 a 1)
        double u = (2.0 * x / constants::IMAGE_WIDTH) - 1.0;
        double v = 1.0 - (2.0 * y / constants::IMAGE_HEIGHT);
        double aspect = static_cast<double>(constants::IMAGE_WIDTH) / constants::IMAGE_HEIGHT;

        // Rayo desde el origen hacia el pixel
        Vector3 origin(0, 0, 0);
        Vector3 direction(u * aspect, v, -1);
        Ray ray(origin, direction);

        return scene.trace(ray);
    }

    // Renderiza el frame completo (todos los píxeles).
    // Param: frame - Vector de salida (resized a IMAGE_WIDTH * IMAGE_HEIGHT).
    void render_frame(std::vector<Vector3>& frame) const {
        frame.resize(constants::IMAGE_WIDTH * constants::IMAGE_HEIGHT);
        for (int y = 0; y < constants::IMAGE_HEIGHT; ++y) {
            for (int x = 0; x < constants::IMAGE_WIDTH; ++x) {
                frame[y * constants::IMAGE_WIDTH + x] = render_pixel(x, y);
            }
        }
    }
};

#endif // RENDERER_H