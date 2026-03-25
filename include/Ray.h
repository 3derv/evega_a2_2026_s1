#ifndef RAY_H
#define RAY_H

#include "Vector3.h"
#include "Constants.h"
#include <cmath>

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

// make_ray con cámara arbitraria: proyecta el pixel (x, y) usando un modelo look-at.
// La cámara en cam_pos apunta siempre al centro de la escena (SCENE_CENTER_*).
//
// Verificación: con cam_pos=(0,0,0) produce la misma dirección que make_ray(x,y)
// porque look-at desde el origen hacia (0,0,-5) reproduce el modelo NDC original.
//
// Param: x, y   — Coordenadas del pixel.
//        cam_pos — Posición de la cámara en el espacio 3D.
inline Ray make_ray(int x, int y, const Vector3& cam_pos) {
    using namespace constants;
    Vector3 scene_center(SCENE_CENTER_X, SCENE_CENTER_Y, SCENE_CENTER_Z);
    Vector3 up_world(0.0, 1.0, 0.0);

    // Base ortonormal de la cámara: forward, right, up
    Vector3 fwd   = (scene_center - cam_pos).normalize();
    Vector3 right = fwd.cross(up_world).normalize();
    Vector3 up    = right.cross(fwd).normalize();

    double u      = (2.0 * x / IMAGE_WIDTH)  - 1.0;
    double v      = 1.0 - (2.0 * y / IMAGE_HEIGHT);
    double aspect = static_cast<double>(IMAGE_WIDTH) / IMAGE_HEIGHT;

    Vector3 direction = fwd + right * (u * aspect) + up * v;
    return Ray(cam_pos, direction);
}

#endif // RAY_H