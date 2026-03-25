#ifndef RENDERER_FACTORY_H
#define RENDERER_FACTORY_H

#include "IRenderer.h"
#include "SequentialRenderer.h"
#include "FinegrainedRenderer.h"
#include "CoarseRenderer.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

// RendererFactory: Factory Pattern con registro dinámico de renderers.
//
// Centraliza la creación de renderers y cumple OCP:
//   Agregar un nuevo modelo requiere añadir UNA entrada en el registro
//   (`available_registry` o `dev_registry`) sin tocar ningún otro método.
//
// DIP: devuelve `unique_ptr<IRenderer>` — el cliente no depende de concretos.
class RendererFactory {
    using FactoryFn = std::function<std::unique_ptr<IRenderer>()>;

    // Registro de modelos disponibles: modelo → factory lambda.
    static const std::unordered_map<std::string, FactoryFn>& available_registry() {
        static const std::unordered_map<std::string, FactoryFn> reg = {
            {"sequential", [] { return std::make_unique<SequentialRenderer>(); }},
            {"fgmt",       [] { return std::make_unique<FinegrainedRenderer>(); }},
            {"cgmt",       [] { return std::make_unique<CoarseRenderer>(); }},
        };
        return reg;
    }

    // Registro de modelos en desarrollo: modelo → mensaje de error.
    static const std::unordered_map<std::string, std::string>& dev_registry() {
        static const std::unordered_map<std::string, std::string> reg = {
            {"smt", "SMT is still in development. Use '--model sequential', '--model fgmt', or '--model cgmt' for now."},
            {"cmp", "CMP is still in development. Use '--model sequential', '--model fgmt', or '--model cgmt' for now."},
        };
        return reg;
    }

public:
    // Crea el renderer para el modelo dado.
    // Lanza std::runtime_error si está en desarrollo, std::invalid_argument si desconocido.
    static std::unique_ptr<IRenderer> create(const std::string& model_name) {
        auto it = available_registry().find(model_name);
        if (it != available_registry().end())
            return it->second();

        auto dev_it = dev_registry().find(model_name);
        if (dev_it != dev_registry().end())
            throw std::runtime_error(dev_it->second);

        throw std::invalid_argument(
            "Unknown model: " + model_name + ". Available: sequential, fgmt, cgmt");
    }

    // Retorna true si el modelo está disponible (no en desarrollo).
    static bool is_available(const std::string& model_name) {
        return available_registry().count(model_name) > 0;
    }

    // Retorna mensaje de ayuda con modelos disponibles.
    static std::string get_help_message() {
        return "Usage: ./raytracer [--model MODEL] [--runs N]\n"
               "Models available: sequential, fgmt, cgmt\n"
               "Models in development: smt, cmp\n"
               "Example: ./raytracer --model cgmt --runs 200\n";
    }
};

#endif // RENDERER_FACTORY_H
