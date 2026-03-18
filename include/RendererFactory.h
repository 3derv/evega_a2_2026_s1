#ifndef RENDERER_FACTORY_H
#define RENDERER_FACTORY_H

#include "IRenderer.h"
#include "SequentialRenderer.h"
#include "FinegrainedRenderer.h"
#include <memory>
#include <string>
#include <stdexcept>

// RendererFactory: Factory Pattern para creación de renderers.
//
// Centraliza la lógica de creación de renderers y permite:
// - Agregar nuevos modelos sin modificar main.cpp
// - Desacoplar main de implementaciones concretas
// - Validar modelos disponibles antes de crear
class RendererFactory {
public:
    // Lista de modelos soportados (para validación)
    static constexpr const char* SUPPORTED_MODELS[] = {
        "sequential", "fgmt"
    };

    // Intenta crear un renderer para el modelo especificado.
    // Lanza excepción si el modelo no existe o aún no está disponible.
    static std::unique_ptr<IRenderer> create(const std::string& model_name) {
        if (model_name == "sequential") {
            return std::make_unique<SequentialRenderer>();
        } else if (model_name == "fgmt") {
            return std::make_unique<FinegrainedRenderer>();
        } else if (model_name == "cgmt") {
            throw std::runtime_error("CGMT is still in development. Use '--model sequential' or '--model fgmt' for now.");
        } else if (model_name == "smt") {
            throw std::runtime_error("SMT is still in development. Use '--model sequential' or '--model fgmt' for now.");
        } else if (model_name == "cmp") {
            throw std::runtime_error("CMP is still in development. Use '--model sequential' or '--model fgmt' for now.");
        } else {
            throw std::invalid_argument("Unknown model: " + model_name + ". Available: sequential, fgmt");
        }
    }

    // Verifica si un modelo está disponible.
    static bool is_available(const std::string& model_name) {
        return model_name == "sequential" || model_name == "fgmt";
    }

    // Retorna mensaje de ayuda con modelos disponibles.
    static std::string get_help_message() {
        return "Usage: ./raytracer [--model MODEL] [--runs N]\n"
               "Models available: sequential, fgmt\n"
               "Models in development: cgmt, smt, cmp\n"
               "Example: ./raytracer --model fgmt --runs 200\n";
    }
};

#endif // RENDERER_FACTORY_H
