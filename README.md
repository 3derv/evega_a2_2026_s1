# Proyecto Individual: Framework Experimental de Modelos de Ejecución Multithreading

## ¿Qué encontrarás aquí?
Código en C++17 para comparar distintos modelos de ejecución concurrente usando ray tracing como problema base.

Modelos implementados:
- **Sequential**: Ejecución secuencial (línea base)
- **FGMT**: Fine-Grained Multithreading (4 threads, quantum por píxel, cache stall simulation)

Modelos en desarrollo:
- CGMT: Coarse-Grained Multithreading
- SMT: Simultaneous Multithreading
- CMP: Chip MultiProcessing

## Estructura del proyecto
```
.
├── src/                    # Código fuente implementacio
├── include/                # Headers (interfaz + implementación)
├── build/                  # Archivos compilados (generado)
├── docs/                   # Documentación (instructions.md)
├── scripts/                # Scripts para mediciones/gráficas
├── results/                # Salidas (CSV, imágenes, gráficas)
├── CMakeLists.txt          # Configuración de compilación
└── README.md              # Este archivo
```

## Uso básico

### Compilar
```bash
mkdir -p build && cd build
cmake .. && make
cd ..
```

### Ejecutar un modelo
```bash
# Sequential (defecto)
./build/raytracer --runs 200

# FGMT
./build/raytracer --model fgmt --runs 200

# Ver ayuda
./build/raytracer --help
```

### Ejecutar con mediciones automáticas
FGMT (200 runs + gráficas):
```bash
./scripts/run_mediciones_fgmt.sh
```

Sequential (200 runs + gráficas):
```bash
./scripts/run_mediciones_secuencial.sh
```

### Generar gráficas desde CSV existente
```bash
python3 scripts/generar_graficas.py results/mediciones_fgmt.csv
python3 scripts/generar_graficas.py results/mediciones_secuencial.csv
```

## Arquitectura de software

El proyecto sigue **principios SOLID**:
- **S**ingle Responsibility: Cada clase tiene una responsabilidad (Renderer, Exporter, CacheModel, etc.)
- **O**pen/Closed: Extensible vía Factory Pattern para agregar nuevos modelos
- **L**iskov Substitution: Todos los renderers heredan de `IRenderer` (polimorfismo)
- **I**nterface Segregation: Interfaces específicas y cohesivas
- **D**ependency Inversion: Depende de abstracciones (`IRenderer`), no de clases concretas

Componentes clave:
- `IRenderer`: Interfaz abstract para todos los modelos
- `SequentialRenderer`: Modelo secuencial
- `FinegrainedRenderer`: Modelo FGMT con 4 threads
- `RendererFactory`: Factory para creación dinámica de renderers
- `GenericRunner`: Orquestador genérico de mediciones
- `CacheModel`: Simulador de cache hits/misses con localidad espacial

## Resultado de mediciones (ejemplo)

**Sequential** (200 runs):
- Promedio: ~0.006 segundos
- Desv.Est: ~0.0001 segundos

**FGMT** (200 runs, 4 threads):
- Promedio: ~0.007 segundos
- Desv.Est: ~0.0008 segundos
- NOPs por thread: ~4,000-4,400 (32 NOPs × ~130-138 cache misses)

## Cómo agregar un nuevo modelo

1. Crear clase que herede de `IRenderer`:
```cpp
class YourRenderer : public IRenderer {
public:
    std::vector<Vector3> render_frame() override { ... }
    std::string get_model_name() const override { return "yourmodel"; }
};
```

2. Actualizar `RendererFactory::create()`:
```cpp
if (model_name == "yourmodel") {
    return std::make_unique<YourRenderer>();
}
```

3. Compilar y ejecutar:
```bash
./build/raytracer --model yourmodel --runs 200
```

## Documentación y reglas del proyecto

Lee `docs/instructions.md` para:
- Reglas obligatorias (200+ mediciones, validación, etc.)
- Observaciones específicas por modelo
- Cómo modelar stalls, clocks, quanta
- Requisitos de validación

## Requisitos

- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.10+
- POSIX threads (pthread)
- Python 3 + pandas/matplotlib (para gráficas)

## Git workflow

```bash
# Ver rama actual
git branch

# Cambiar a rama (p. ej. desarrollo de CGMT)
git checkout -b cgmt

# Commits incrementales
git add file.h file.cpp
git commit -m "type(component): descripción corta"

# Mergear a develop cuando esté completo
git checkout develop
git merge cgmt
```

## Contacto y créditos

Proyecto para curso CE4302 - Arquitectura de Computadores II.
Semestre I, 2026.

