#ifndef IRENDERER_H
#define IRENDERER_H

#include <vector>
#include <string>
#include "Vector3.h"

// IRenderer: Interfaz abstracta para renderizadores.
//
// Define el contrato que todos los modelos de ejecución deben cumplir.
// Permite abstraer detalles de implementación (secuencial, FGMT, CGMT, etc.)
// y habilita el uso de Factory Pattern para creación dinámica de renderers.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Renderiza un frame completo y retorna el buffer de píxeles.
    // Return: Vector de colores (Vector3) con dimensiones IMAGE_WIDTH × IMAGE_HEIGHT.
    virtual std::vector<Vector3> render_frame() = 0;

    // Retorna el nombre del modelo para debugging/logging.
    virtual std::string get_model_name() const = 0;
};

#endif // IRENDERER_H
