# Framework Experimental de Modelos de Ejecución Multithreading

Comparación de modelos de ejecución concurrente usando ray tracing como carga de trabajo.
Cada modelo simula cómo un procesador organiza sus threads y maneja los stalls de memoria.

## Modelos implementados

| Modelo | Scheduler | Rotación | Costo stall | VT |
|--------|-----------|----------|-------------|----|
| **Sequential** | 1 hilo | — | `CACHE_MISS_PENALTY_NS = 3200 ns` | suma |
| **FGMT** | 4 contextos, 1 pipeline | Cada ciclo (obligatoria) | `PIXEL_QUANTUM_NS = 1000 ns` (quantum perdido) | suma |
| **CGMT** | 4 contextos, 1 pipeline | Solo en stall | 0 ns (stall oculto, CPI ideal = 10.0) | suma |
| **SMT** | Simulación pura W=2 | Por slot disponible | 0 ns (stall oculto + W=2) | `global_clock × Q` |
| **CMP** | N=4 OS threads, cores físicos | Paralelo real | `CACHE_MISS_PENALTY_NS` por core (sin ocultar) | `max(VT por core)` |

## Estructura del proyecto

```
.
├── src/                        # Implementaciones (.cpp)
│   ├── main.cpp                # CLI, Factory, GenericRunner, salida de stats
│   ├── SequentialRenderer.cpp  # Modelo baseline (1 hilo)
│   ├── FinegrainedRenderer.cpp # Modelo FGMT (semáforos por thread)
│   ├── CoarseRenderer.cpp      # Modelo CGMT (scheduler round-robin)
│   ├── SMTRenderer.cpp         # Modelo SMT (simulación pura W=2, sin OS threads)
│   ├── CMPRenderer.cpp         # Modelo CMP (N=4 cores, OS threads reales)
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
./build/raytracer --model sequential
./build/raytracer --model fgmt
./build/raytracer --model cgmt
./build/raytracer --model smt
./build/raytracer --model cmp
./build/raytracer --help
```

Salida por stdout incluye: tiempo real (avg/min/max/stddev), tiempo virtual (ns),
stalls promedio, CPI y estadísticas por thread (todos los modelos paralelos).

### 3. Ejecutar todos los modelos y generar análisis completo

```bash
# Compila, ejecuta los 5 modelos, valida imágenes y genera gráficas
./scripts/run_all_models.sh
./scripts/run_all_models.sh --skip-fgmt            # omitir FGMT (~100s)
./scripts/run_all_models.sh --skip-fgmt --with-perf  # incluir perf stat
```

Flags disponibles: `--skip-fgmt`, `--skip-smt`, `--skip-cmp`, `--with-perf`.

Genera en `results/`:
- `mediciones_*.csv` — 200 mediciones por modelo
- `speedup_report.log` — speedup, eficiencia paralela, escalabilidad, CPI, stalls
- `graficas/` — 10 gráficas PNG comparativas (ver tabla abajo)
- `image/` — `frame_*.ppm` de cada modelo (deben ser byte-exactas)

| Gráfica | Descripción |
|---------|-------------|
| `01_histogram_comparativo.png` | Histograma de VT por frame, eje X compartido |
| `02_boxplot_comparativo.png` | Boxplot VT — todos los modelos |
| `03_speedup_comparison.png` | Speed Up VT vs Sequential |
| `04_timeline_executions.png` | VT por frame (animación 200 frames) |
| `05_virtual_time_comparison.png` | VT promedio: escala completa + overhead |
| `06_cpu_speedup_smt_cmp.png` | Speed Up CPU wall-clock — SMT y CMP |
| `07_efficiency.png` | **Eficiencia paralela** E = S/N por modelo |
| `08_scalability.png` | **Escalabilidad**: N vs Speedup real vs ideal |
| `09_modeled_group_comparison.png` | **Grupo simulado**: seq vs fgmt vs cgmt (VT) |
| `10_real_group_comparison.png` | **Grupo real**: seq vs smt vs cmp (CPU) |

### 4. Ejecutar un solo modelo con script individual

```bash
./scripts/run_mediciones_secuencial.sh   # sequential, genera gráfica propia
./scripts/run_mediciones_fgmt.sh         # fgmt vs sequential
./scripts/run_mediciones_cgmt.sh         # sequential + fgmt + cgmt
./scripts/run_mediciones_cmp.sh          # cmp vs sequential
```

### 5. Generar GIF de animación

```bash
./scripts/make_gif_sequential.sh   # GIF del modelo sequential
./scripts/make_gif_cmp.sh          # GIF del modelo CMP
```

### 6. Comparativa SMT ON vs OFF (hardware real)

Mide el modelo CMP con Hyper-Threading activado y desactivado para cuantificar
el efecto del SMT hardware en el rendimiento real. Requiere `sudo`.

```bash
./scripts/run_smt_comparison.sh
# Controlar manualmente:
echo off | sudo tee /sys/devices/system/cpu/smt/control   # desactivar
echo on  | sudo tee /sys/devices/system/cpu/smt/control   # activar
cat /sys/devices/system/cpu/smt/control                   # ver estado
```

Guarda `results/mediciones_cmp_smt_on.csv`, `mediciones_cmp_smt_off.csv` y
la gráfica `results/graficas/11_smt_on_vs_off.png`.

### 7. Perfilado con `perf stat`

Captura contadores de hardware (cycles, instructions, cache-misses, branches)
y software (task-clock, context-switches) para cualquier modelo.
Referencia: <https://perfwiki.github.io/main/>

```bash
./scripts/run_perf.sh                        # todos los modelos
./scripts/run_perf.sh sequential cmp smt     # modelos específicos
```

Guarda los resultados en `results/perf/perf_<modelo>.txt`. Las métricas de CPI,
IPC y tasa de cache-miss permiten correlacionar con los modelos de ejecución.

## Reloj virtual

Cada modelo acumula tiempo virtual (ns) para modelar la carga sobre el pipeline,
independientemente del tiempo real del SO.

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `PIXEL_QUANTUM_NS` | 1000 ns | 1 slot de pipeline por pixel |
| `NOP_PENALTY_NS` | 100 ns | 1 ciclo base (usado en `CACHE_MISS_PENALTY_NS`) |
| `CACHE_MISS_PENALTY_NS` | 3200 ns | Stall completo = 32 NOPs (Sequential y CMP por core) |
| `CACHE_SIZE` | 256 B | Miss rate ~25% → ~409 stalls/frame (diferencias visibles) |
| `CMP_NUM_CORES` | 4 | Núcleos independientes en CMP |

**CPI reportado**: `virtual_time_ns / (PIXEL_QUANTUM_NS × total_pixels)`. CPI ideal CGMT = 10.0.

**Resultados verificados** (200 frames, CACHE_SIZE=256, hardware físico):

| Modelo | Métrica | VT/CPU prom | SpeedUp | Eficiencia (E=S/N) | CPI |
|------------|---------|-------------|---------|---------------------|-----|
| Sequential | VT | 6.110 ms | 1.00× | 1.000 (baseline) | 12.73 |
| FGMT | VT | 5.215 ms | 1.17× | 0.293 (N=4) | 10.87 |
| CGMT | VT | 4.800 ms | 1.27× | 0.318 (N=4) | 10.00 |
| SMT | CPU | ~1.04 ms | 2.51×\* | — | 5.07 |
| CMP | CPU | ~1.00 ms | 3.95×\* | — | 3.22 |

\* SpeedUp de VT (reloj simulado). CPU wall-clock de SMT/CMP varía con carga del SO.

## Arquitectura de software

- **SOLID** — `RendererFactory` usa registry map (OCP); `GenericRunner` depende de `IRenderer` (DIP); `switch_to_next_thread()` extrae responsabilidad del scheduler (SRP).
- **DRY** — `make_ray()` en `Ray.h` compartida por todos los renderers; `reset_thread_stats()` en `RendererUtils.h`; rutas de archivos resueltas una sola vez en `Exporter`.

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

