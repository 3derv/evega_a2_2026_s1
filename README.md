# Framework Experimental de Modelos de Ejecución Multithreading

Comparación de modelos de ejecución concurrente usando ray tracing como carga de trabajo.
Cada modelo simula cómo un procesador organiza sus threads y maneja los stalls de memoria.

## Modelos implementados

| Modelo | Scheduler | Rotación | Costo stall |
|--------|-----------|----------|--------------|
| **Sequential** | Ninguno (1 hilo) | — | `CACHE_MISS_PENALTY_NS = 3200 ns` (stall completo) |
| **FGMT** | Reloj global (`clock % NUM_THREADS == tid`) | Cada ciclo (obligatoria) | `NOP_PENALTY_NS = 100 ns` (stall oculto) |
| **CGMT** | `current_thread` + `switch_to_next_thread()` | Solo en stall | `PIXEL_QUANTUM_NS + CONTEXT_SWITCH_COST_NS = 1400 ns` |

Modelos en desarrollo: SMT, CMP.

## Estructura del proyecto

```
.
├── src/                        # Implementaciones (.cpp)
│   ├── main.cpp                # CLI, Factory, GenericRunner, salida de stats
│   ├── SequentialRenderer.cpp  # Modelo baseline
│   ├── FinegrainedRenderer.cpp # Modelo FGMT con reloj global explícito
│   ├── CoarseRenderer.cpp      # Modelo CGMT con scheduler round-robin
│   └── Exporter.cpp            # Guardar PPM e imagen CSV
├── include/                    # Headers
│   ├── IRenderer.h             # Interfaz abstracta (polimorfismo)
│   ├── RendererFactory.h       # Factory con registry map (OCP)
│   ├── GenericRunner.h         # Orquestador universal de mediciones (DIP)
│   ├── RendererUtils.h         # Helpers compartidos DRY (reset, sum_vt)
│   ├── Ray.h                   # Ray + make_ray() compartida (DRY)
│   ├── CacheModel.h            # Simulador de cache miss con localidad espacial
│   ├── Constants.h             # Constantes centralizadas (dimensiones, rutas, VT)
│   └── Metrics.h               # Structs ThreadMetrics / Metrics
├── scripts/                    # Scripts de medición y análisis
├── results/                    # CSVs, imágenes PPM, gráficas PNG, log
├── docs/                       # instructions.md (reglas del proyecto)
├── CMakeLists.txt
└── README.md
```

## Uso rápido

### 1. Compilar

```bash
mkdir -p build && cd build
cmake .. && make -j4
cd ..
```

### 2. Ejecutar un modelo

```bash
./build/raytracer --model sequential --runs 200
./build/raytracer --model fgmt      --runs 200
./build/raytracer --model cgmt      --runs 200
./build/raytracer --help
```

Salida por stdout incluye: tiempo real (avg/min/max/stddev), tiempo virtual (ns),
stalls promedio, CPI y estadísticas por thread (FGMT y CGMT).

### 3. Ejecutar todos los modelos y generar análisis completo

```bash
# Uso recomendado — compila, ejecuta los 3 modelos, valida imágenes y genera gráficas
./scripts/run_all_models.sh          # 200 runs (por defecto)
./scripts/run_all_models.sh 500      # número de runs personalizado
```

Genera en `results/`:
- `mediciones_secuencial.csv`, `mediciones_fgmt.csv`, `mediciones_cgmt.csv`
- `speedup_report.log` — reporte de speedup, stalls, CPI e información PPM
- `graficas/` — 5 gráficas PNG comparativas
- `image/` — `frame_*.ppm` de cada modelo (deben ser byte-exactas)

### 4. Ejecutar un solo modelo con script individual

```bash
./scripts/run_mediciones_secuencial.sh   # sequential, genera gráfica propia
./scripts/run_mediciones_fgmt.sh         # fgmt vs sequential
./scripts/run_mediciones_cgmt.sh         # sequential + fgmt + cgmt
```

### 5. Análisis desde CSVs existentes

```bash
# Reporte de speedup + gráficas (requiere los 3 CSVs ya generados)
python3 scripts/analizar_speedup.py \
    results/mediciones_secuencial.csv \
    results/mediciones_fgmt.csv \
    results/mediciones_cgmt.csv \
    --graphs results/graficas \
    --log    results/speedup_report.log

# Gráfica individual desde un CSV
python3 scripts/generar_graficas.py results/mediciones_fgmt.csv
```

## Reloj virtual

Cada modelo acumula tiempo virtual (ns) para modelar la carga sobre el pipeline,
independientemente del tiempo real del SO.

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `PIXEL_QUANTUM_NS` | 1000 ns | 1 slot de pipeline = 10 ciclos × 100 ns |
| `NOP_PENALTY_NS` | 100 ns | 1 ciclo NOP (FGMT: detección de miss) |
| `CACHE_MISS_PENALTY_NS` | 3200 ns | Stall completo = 32 NOPs (Solo Sequential) |
| `CONTEXT_SWITCH_COST_NS` | 400 ns | Overhead de context switch (Solo CGMT) |

**CPI reportado**: `virtual_time_ns / (NOP_PENALTY_NS × total_pixels)`. CPI ideal = 10.0.

## Arquitectura de software

- **SOLID** — `RendererFactory` usa registry map (OCP); `GenericRunner` depende de `IRenderer` (DIP); `switch_to_next_thread()` extrae responsabilidad del scheduler (SRP).
- **DRY** — `make_ray()` en `Ray.h` compartida por los 3 renderers; `reset_thread_stats()` y `sum_virtual_times()` en `RendererUtils.h`; rutas de archivos resueltas una sola vez en `Exporter`.

## Cómo agregar un nuevo modelo

1. Crear `include/MiRenderer.h` y `src/MiRenderer.cpp` heredando de `IRenderer`.
2. Agregar **una entrada** en `available_registry()` de `RendererFactory.h`:
   ```cpp
   {"mimodelo", [] { return std::make_unique<MiRenderer>(); }},
   ```
3. Agregar las rutas en `Constants.h` (`IMAGE_FILE_*`, `CSV_FILE_*`).
4. Agregar el source en `CMakeLists.txt`.

No es necesario modificar `main.cpp`, `GenericRunner`, `Exporter` ni ningún otro archivo.

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

