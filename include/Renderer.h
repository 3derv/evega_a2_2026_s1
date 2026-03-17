#ifndef RENDERER_H
#define RENDERER_H

#include "Scene.h"
#include "Vector3.h"
#include <vector>

const int WIDTH = 640;
const int HEIGHT = 480;

struct Renderer {
    Scene scene;

    Vector3 render_pixel(int x, int y) const {
        // Coordenadas normalizadas (-1 a 1)
        double u = (2.0 * x / WIDTH) - 1.0;
        double v = 1.0 - (2.0 * y / HEIGHT);
        double aspect = static_cast<double>(WIDTH) / HEIGHT;

        // Rayo desde el origen hacia el pixel
        Vector3 origin(0, 0, 0);
        Vector3 direction(u * aspect, v, -1);
        Ray ray(origin, direction);

        return scene.trace(ray);
    }

    void render_frame(std::vector<Vector3>& frame) const {
        frame.resize(WIDTH * HEIGHT);
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                frame[y * WIDTH + x] = render_pixel(x, y);
            }
        }
    }
};

#endif // RENDERER_H