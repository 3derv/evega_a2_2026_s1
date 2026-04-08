# Framework de Modelos de Ejecución Multithreading

Framework experimental en C++17 que compara cinco modelos de ejecución concurrente
usando ray tracing (80×60 px, 3 esferas, 200 frames de animación) como carga de trabajo
representativa de una aplicación memory-bound en lazo de datos.

El objetivo es **modelar schedulers de hardware** — no medir el scheduler del SO.
Cada modelo acumula un reloj virtual (VT) independiente del tiempo real del sistema,
lo que hace los resultados reproducibles y comparables entre máquinas.

## Modelos implementados

| Modelo | Threads hardware | Scheduler | Stall oculto | VT semántica |
|--------|-----------------|-----------|-------------|--------------|
| **Sequential** | 1 | — | ✗ paga `CACHE_MISS_PENALTY_NS` completo | suma |
| **FGMT** | 4 contextos, 1 pipeline | Rota cada ciclo (obligatorio) | Parcial — pierde 1 quantum (`PIXEL_QUANTUM_NS`) | suma |
| **CGMT** | 4 contextos, 1 pipeline | Rota solo en stall | ✓ stall oculto, paga solo `CONTEXT_SWITCH_COST_NS` | suma |
| **SMT** | 4 contextos, W=2 issue slots | Simulación pura, sin OS threads | ✓ stall oculto + W=2 simultáneo | `global_clock × Q` |
| **CMP** | N=4 cores físicos, OS threads reales | Paralelo real | ✗ cada core paga su propio stall | `max(VT por core)` |

## Estructura del proyecto

```
.
├── src/
│   ├── main.cpp                # CLI: --model, --runs, --verbose, --gif
│   ├── SequentialRenderer.cpp  # Baseline: 1 hilo, stall completo
│   ├── FinegrainedRenderer.cpp # FGMT: semáforos, rota cada ciclo
│   ├── CoarseRenderer.cpp      # CGMT: mutex/condvar, rota solo en stall
│   ├── SMTRenderer.cpp         # SMT: simulación pura W=2, sin OS threads
│   ├── CMPRenderer.cpp         # CMP: N=4 OS threads en paralelo real
│   └── Exporter.cpp            # Guardar PPM y CSV
├── include/
│   ├── IRenderer.h             # Interfaz abstracta (polimorfismo)
│   ├── RendererFactory.h       # Factory con registry map (OCP)
│   ├── GenericRunner.h         # Orquestador universal de mediciones (DIP)
│   ├── RendererUtils.h         # Helpers DRY: reset_thread_stats, sum_virtual_times
│   ├── Ray.h                   # Ray + make_ray() compartida por todos los renderers
│   ├── CacheModel.h            # Simulador de cache miss con localidad espacial
│   ├── Constants.h             # Todas las constantes: dimensiones, rutas, VT
│   ├── CameraPath.h            # Órbita elíptica de cámara (200 frames)
│   └── Metrics.h               # Structs ThreadMetrics / Metrics
├── scripts/
│   ├── run_all_models.sh        # Orquestador principal (ver abajo)
│   ├── analizar_speedup.py      # Speedup, IC95%, eficiencia, escalabilidad + 10 gráficas
│   ├── run_perf.sh              # perf stat para todos los modelos
│   ├── perf_to_json.py          # Convierte perf_*.txt → perf_results.json
│   ├── run_smt_comparison.sh    # CMP con SMT hardware ON vs OFF
│   ├── run_scalability_matrix.py/sh  # Matriz de escalabilidad N × configuración
│   ├── run_thread_sweep.py      # Sweep de hilos N=2..8 para escalabilidad
│   ├── make_gif_sequential.sh   # GIF animación sequential
│   └── make_gif_cmp.sh          # GIF animación CMP
├── results/
│   ├── mediciones_*.csv         # 200 mediciones por modelo
│   ├── image/frame_*.ppm        # Frame de referencia por modelo
│   ├── graficas/                # 10+ gráficas PNG
│   └── perf/                    # Salida de perf stat por modelo
├── docs/
│   ├── instructions.md          # Reglas del proyecto y especificaciones
│   └── analisis_tecnico.md      # Análisis teórico vs. observado por modelo
├── tests/gif_utils/             # Frames PPM intermedios para GIF (generados, no versionar)
├── CMakeLists.txt
└── README.md
```

## Uso rápido

### 1. Compilar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build/ -j4
```

### 2. Ejecutar un modelo

```bash
./build/raytracer --model sequential
./build/raytracer --model fgmt
./build/raytracer --model cgmt
./build/raytracer --model smt
./build/raytracer --model cmp
./build/raytracer --model smt --verbose 30  # log ciclo a ciclo (todos los modelos)
./build/raytracer --help
```

La stdout incluye: tiempo real (avg/min/max/σ), tiempo virtual promedio (ns/ms),
stalls promedio, CPI y estadísticas por thread (modelos paralelos).

### 3. Ejecutar todos los modelos y generar análisis completo

```bash
./scripts/run_all_models.sh
./scripts/run_all_models.sh --skip-fgmt            # omitir FGMT (~100s)
./scripts/run_all_models.sh --skip-fgmt --with-perf  # incluir perf stat
```

Flags disponibles: `--skip-fgmt`, `--skip-smt`, `--skip-cmp`, `--with-perf`.

Genera en `results/`:

| Artefacto | Descripción |
|-----------|-------------|
| `mediciones_*.csv` | 200 mediciones por modelo (frames de animación) |
| `speedup_report.log` | Speedup, IC95%, eficiencia paralela, escalabilidad, CPI |
| `image/frame_*.ppm` | Frame de referencia por modelo (deben ser byte-exactas) |
| `graficas/01_histogram_comparativo_gmt.png` | Histograma VT agrupado: FGMT vs CGMT |
| `graficas/01_histogram_comparativo_s-cmp.png` | Histograma VT agrupado: SMT vs CMP |
| `graficas/02_boxplot_comparativo_gmt.png` | Boxplot VT agrupado: FGMT vs CGMT |
| `graficas/02_boxplot_comparativo_s-cmp.png` | Boxplot VT agrupado: SMT vs CMP |
| `graficas/03_speedup_comparison.png` | Speed Up VT vs Sequential con IC95% |
| `graficas/04_timeline_executions.png` | VT por frame (animación 200 frames) |
| `graficas/05_virtual_time_comparison.png` | VT promedio: escala completa |
| `graficas/06_cpu_speedup_smt_cmp.png` | Speed Up CPU wall-clock — SMT y CMP |
| `graficas/07_efficiency.png` | Eficiencia paralela E = S/N por modelo |
| `graficas/08_scalability.png` | Escalabilidad: N vs Speedup real vs ideal (Amdahl) |
| `graficas/09_modeled_group_comparison.png` | Grupo simulado: seq vs fgmt vs cgmt (VT) |
| `graficas/10_real_group_comparison.png` | Grupo real: seq vs smt vs cmp (CPU time) |

### 4. Generar GIF de animación

```bash
./scripts/make_gif_sequential.sh   # GIF orbital del modelo sequential
./scripts/make_gif_cmp.sh          # GIF orbital del modelo CMP
```

Ambos scripts generan los 200 frames PPM si no existen y ensamblan el GIF
usando FFmpeg (o Pillow como fallback).

### 5. Comparativa SMT ON vs OFF (hardware físico)

Mide el modelo CMP con Hyper-Threading activado y desactivado para cuantificar
el efecto del SMT hardware en el rendimiento real. Requiere `sudo`.

```bash
./scripts/run_smt_comparison.sh

# Control manual:
echo off | sudo tee /sys/devices/system/cpu/smt/control   # desactivar HT
echo on  | sudo tee /sys/devices/system/cpu/smt/control   # activar HT
cat /sys/devices/system/cpu/smt/control                   # estado actual
```

### 6. Sweep de escalabilidad (N hilos)

Barre N=2..8 hilos, recompilando con cada N, y genera `results/thread_sweep.csv`
para graficar speedup y eficiencia en función del número de contextos/cores.

```bash
python3 scripts/run_thread_sweep.py          # sweep completo
python3 scripts/run_thread_sweep.py --max-n 4 # limitar a N=4
```

### 7. Matriz de escalabilidad

Ejecuta combinaciones de N × configuración (cache size, issue width) para
explorar el espacio de diseño. Guarda resultados con timestamp en
`results/experiments_scaling/`.

```bash
python3 scripts/run_scalability_matrix.py
# o con el wrapper bash:
./scripts/run_scalability_matrix.sh
```

### 8. Perfilado con `perf stat`

Captura contadores de hardware (cycles, instructions, cache-misses, branch-misses)
para correlacionar con los modelos de ejecución.

```bash
sudo ./enable_perf.sh enable            # bajar perf_event_paranoid
./scripts/run_perf.sh                   # todos los modelos
./scripts/run_perf.sh sequential cmp    # modelos específicos
sudo ./enable_perf.sh disable           # restaurar
```

Guarda `results/perf/perf_<modelo>.txt` y genera `results/perf/perf_results.json`
con métricas estructuradas (CPI hardware, IPC, cache-miss rate, branch-miss rate).

## Reloj virtual (VT)

Cada modelo acumula un reloj virtual independiente del planificador del SO,
modelando el tiempo que tardaría el pipeline de hardware en completar el frame.

| Constante | Valor | Quién la paga |
|-----------|-------|---------------|
| `PIXEL_QUANTUM_NS` | 1 000 ns | Todos los modelos (1 slot de pipeline) |
| `NOP_PENALTY_NS` | 100 ns | Base de `CACHE_MISS_PENALTY_NS` |
| `CACHE_MISS_PENALTY_NS` | 3 200 ns | Sequential y CMP por core (stall completo) |
| `CONTEXT_SWITCH_COST_NS` | 400 ns | CGMT al cambiar de contexto en stall |
| `CACHE_SIZE` | 256 B | Miss rate ~25% → ~1 200 stalls/frame |
| `CMP_NUM_CORES` | 4 | Núcleos independientes en CMP |
| `SMT_ISSUE_WIDTH` | 2 | Slots simultáneos por ciclo en SMT |

**CPI reportado**: `VT / (PIXEL_QUANTUM_NS × total_pixels)`.  
CPI ideal CGMT = 10.0 (stalls completamente ocultos, solo paga `CONTEXT_SWITCH_COST_NS`).

**Semántica de VT por modelo:**
- **Sequential / FGMT / CGMT** — VT = **suma** de tiempos de todos los threads (pipeline compartido, ejecución serial intercalada).
- **SMT** — VT = `global_clock × PIXEL_QUANTUM_NS` (reloj de pared de la simulación; con W=2 el pipeline completa ~2 px/ciclo).
- **CMP** — VT = `max(VT_core_i)` (los cores avanzan en paralelo real; el reloj de pared avanza al ritmo del más lento).

**Resultados de referencia** (200 frames, CACHE_SIZE=256, medición WSL2 — hardware físico diferirá):

| Modelo | VT prom | SpeedUp VT | Eficiencia E=S/N | CPI |
|--------|---------|-----------|-----------------|-----|
| Sequential | 6.110 ms | 1.00× | 1.000 (baseline) | 12.73 |
| FGMT | 5.215 ms | 1.17× | 0.293 (N=4) | 10.87 |
| CGMT | 4.800 ms | 1.27× | 0.318 (N=4) | 10.00 |
| SMT | 2.436 ms | 2.51× | — | 5.07 |
| CMP | 1.547 ms | 3.95× | 0.988 (N=4) | 3.22 |

Ver [docs/analisis_tecnico.md](docs/analisis_tecnico.md) para el análisis teórico vs. observado completo.

## Arquitectura de software (SOLID + DRY)

- **OCP** — `RendererFactory` usa un `unordered_map` como registry. Agregar un modelo nuevo no toca ningún archivo existente.
- **DIP** — `GenericRunner` y `main.cpp` dependen únicamente de `IRenderer*`; nunca de concretos.
- **SRP** — `switch_to_next_thread()` (CGMT) extrae la lógica del scheduler en su propio método; `Exporter` es responsable exclusivo de rutas y E/S.
- **DRY** — `make_ray()` en `Ray.h` compartida por todos los renderers. `reset_thread_stats()` y `sum_virtual_times()` en `RendererUtils.h` eliminan duplicación entre FGMT, CGMT y CMP.

## Personalización de parámetros

Todas las constantes del framework se centralizan en `include/Constants.h`.
Para experimentar con distintas configuraciones basta editar ese archivo y recompilar:

```bash
# Ejemplo: duplicar el tamaño de cache y reducir la resolución
# Editar include/Constants.h:
#   CACHE_SIZE = 512
#   IMAGE_WIDTH = 40, IMAGE_HEIGHT = 30
cmake --build build/ -j4
```

Parámetros ajustables: `IMAGE_WIDTH`, `IMAGE_HEIGHT`, `NUM_FRAMES`,
`CACHE_SIZE`, `PIXEL_QUANTUM_NS`, `CACHE_MISS_PENALTY_NS`,
`CMP_NUM_CORES`, `SMT_ISSUE_WIDTH`.

## Cómo agregar un nuevo modelo

1. Crear `include/MiRenderer.h` y `src/MiRenderer.cpp` heredando de `IRenderer`.
2. Agregar **una entrada** en `available_registry()` de `RendererFactory.h`:
   ```cpp
   {"mimodelo", [] { return std::make_unique<MiRenderer>(); }},
   ```
3. Agregar las rutas de salida en `Constants.h` (`IMAGE_FILE_MIMODELO`, `CSV_FILE_MIMODELO`).
4. Agregar el `.cpp` en `CMakeLists.txt`.

No es necesario modificar `main.cpp`, `GenericRunner`, `Exporter` ni ningún otro archivo.

## Requisitos

- Compilador C++17 (GCC 7+ / Clang 5+)
- CMake 3.10+
- POSIX `pthread` + `semaphore.h`
- Python 3 con `pandas` y `matplotlib` (para análisis y gráficas)
- `perf` de Linux (opcional, para contadores de hardware)

## Documentación

| Archivo | Contenido |
|---------|-----------|
| [docs/instructions.md](docs/instructions.md) | Reglas del proyecto: modelos obligatorios, métricas, validación |
| [docs/analisis_tecnico.md](docs/analisis_tecnico.md) | Análisis teórico vs. observado, Ley de Amdahl, limitaciones WSL2 |

## Contacto

Proyecto CE4302 — Arquitectura de Computadores II, Semestre I 2026.

