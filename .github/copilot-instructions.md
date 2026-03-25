# Contexto del proyecto — Framework de Modelos de Ejecución Multithreading

## Propósito
Framework experimental en **C++17** que compara tres modelos de ejecución concurrente
usando ray tracing (640×480, 3 esferas) como carga de trabajo.
El objetivo es modelar schedulers de hardware, NO medir el scheduler del SO.

## Modelos implementados
- **Sequential** — 1 hilo, stall completo (`CACHE_MISS_PENALTY_NS = 3200 ns`).
- **FGMT** — 4 threads, reloj global explícito (`global_clock_ % NUM_THREADS == tid`),
  rotación obligatoria cada ciclo, stall oculto (`NOP_PENALTY_NS = 100 ns`).
- **CGMT** — 4 threads, `current_thread` + `switch_to_next_thread()`,
  rotación solo en stall (`PIXEL_QUANTUM_NS + CONTEXT_SWITCH_COST_NS`).

## Reglas de código (siempre respetar)
- **SOLID**: `RendererFactory` usa registry `unordered_map` (OCP); `GenericRunner` depende
  de `IRenderer` (DIP); cada responsabilidad en su clase (SRP).
- **DRY**: Construcción del rayo → `make_ray()` en `Ray.h`.
  Reset de threads → `reset_thread_stats()` en `RendererUtils.h`.
  Suma de VT → `sum_virtual_times()` en `RendererUtils.h`.
  Rutas de archivos → resueltas en `Exporter`, expuestas por `get_image_file()`/`get_csv_file()`.
- Agregar modelo = 1 entrada en `available_registry()` + 2 constantes en `Constants.h`
  + source en `CMakeLists.txt`. Sin tocar `main.cpp` ni `GenericRunner`.
- Documentar funciones no triviales con comentarios explicativos del *por qué*.

## Reloj virtual (no confundir con tiempo real)
| Constante | Valor | Quién la paga |
|-----------|-------|---------------|
| `PIXEL_QUANTUM_NS` | 1000 ns | Todos (1 slot de pipeline) |
| `NOP_PENALTY_NS` | 100 ns | FGMT (stall oculto) |
| `CACHE_MISS_PENALTY_NS` | 3200 ns | Solo Sequential |
| `CONTEXT_SWITCH_COST_NS` | 400 ns | Solo CGMT (si hubo cambio real) |

VT total = **suma** de threads (pipeline compartido, no paralelo).
CPI = `virtual_time_ns / (NOP_PENALTY_NS × total_pixels)`. CPI ideal = 10.0.

## Archivos clave
- `include/IRenderer.h` — interfaz abstracta de todos los renderers.
- `include/RendererFactory.h` — factory con registry map.
- `include/GenericRunner.h` — orquestador universal (usa `IRenderer*`).
- `include/RendererUtils.h` — helpers DRY compartidos.
- `include/Constants.h` — todas las constantes (dimensiones, rutas, VT).
- `src/FinegrainedRenderer.cpp` — scheduler FGMT con `pipeline_mutex_`/`clock_tick_`.
- `src/CoarseRenderer.cpp` — scheduler CGMT con `sched_mutex`/`sched_cv`/`switch_to_next_thread()`.

## Correctness
Los 3 modelos deben producir imágenes PPM byte-exactas.
Verificar siempre con `diff results/image/frame_*.ppm`.

## Scripts
```bash
./scripts/run_all_models.sh [NUM_RUNS]   # flujo completo recomendado
./build/raytracer --model [sequential|fgmt|cgmt] --runs N
```
