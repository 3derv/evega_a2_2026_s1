#ifndef CAMERA_PATH_H
#define CAMERA_PATH_H

#include "Vector3.h"
#include "Constants.h"
#include <cmath>

// camera_pos_for_frame: Posición de la cámara sobre una órbita elíptica en el plano XZ.
//
// La órbita está centrada en (SCENE_CENTER_X, CAMERA_ORBIT_Y, SCENE_CENTER_Z).
// Semi-eje mayor CAMERA_ORBIT_RX en X, semi-eje menor CAMERA_ORBIT_RZ en Z.
// Avanza 1° por frame → 200 frames cubren 200° del elipse.
//
// Propiedad notable: a los 90° la cámara pasa exactamente por (0,0,0), que es la
// posición original de referencia → frame 90 reproduce la imagen baseline.
inline Vector3 camera_pos_for_frame(int frame) {
    const double angle_rad = frame * (M_PI / 180.0);
    return Vector3(
        constants::SCENE_CENTER_X + constants::CAMERA_ORBIT_RX * std::cos(angle_rad),
        constants::CAMERA_ORBIT_Y,
        constants::SCENE_CENTER_Z + constants::CAMERA_ORBIT_RZ * std::sin(angle_rad)
    );
}

#endif // CAMERA_PATH_H
