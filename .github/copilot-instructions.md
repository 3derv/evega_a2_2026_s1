# Contexto del proyecto — Framework de Modelos de Ejecución Multithreading

## Propósito
Framework experimental en **C++17** que compara modelos de ejecución concurrente
usando ray tracing (80×60, 3 esferas, 200 frames animados) como carga de trabajo.
El objetivo es modelar schedulers de hardware, NO medir el scheduler del SO.

## Modelos implementados
- **Sequential** — 1 hilo, stall completo (`CACHE_MISS_PENALTY_NS = 3200 ns`).
- **FGMT** — 4 contextos, 1 pipeline compartido. Rota cada ciclo; stall = 1 quantum perdido.
- **CGMT** — 4 contextos, 1 pipeline. Rota solo en stall (0 VT pagado, stall oculto).
- **SMT** — Simulación pura W=2; 0 OS threads. VT = `global_clock_ × PIXEL_QUANTUM_NS`.
- **CMP** — N=4 cores con OS threads reales. VT = `max(VT por core)`.

## Reglas de código (siempre respetar)
- **SOLID**: `RendererFactory` usa registry `unordered_map` (OCP); `GenericRunner` depende
  de `IRenderer` (DIP); cada responsabilidad en su clase (SRP).
- **DRY**: Construcción del rayo → `make_ray()` en `Ray.h`.
  Reset de threads → `reset_thread_stats()` en `RendererUtils.h`.
  Rutas de archivos → resueltas en `Exporter`, expuestas por `get_image_file()`/`get_csv_file()`.
- Agregar modelo = 1 entrada en `available_registry()` + constantes en `Constants.h`
  + source en `CMakeLists.txt`. Sin tocar `main.cpp` ni `GenericRunner`.
- Documentar funciones no triviales con comentarios explicativos del *por qué*.

## Reloj virtual (no confundir con tiempo real)
| Constante | Valor | Quién la paga |
|-----------|-------|---------------|
| `PIXEL_QUANTUM_NS` | 1000 ns | Todos (1 slot de pipeline) |
| `NOP_PENALTY_NS` | 100 ns | Base del cálculo (FGMT/Sequential) |
| `CACHE_MISS_PENALTY_NS` | 3200 ns | Sequential completo; FGMT paga `PIXEL_QUANTUM_NS` |
| `CMP_NUM_CORES` | 4 | Cores independientes; VT = max, no suma |

FGMT/CGMT/Sequential: VT = **suma** de threads (pipeline compartido).
SMT/CMP: VT refleja reloj de pared real (no suma).
CPI ideal CGMT = 10.0 (stalls completamente ocultos).

## Archivos clave
- `include/IRenderer.h` — interfaz abstracta de todos los renderers.
- `include/RendererFactory.h` — factory con registry map.
- `include/GenericRunner.h` — orquestador universal (usa `IRenderer*`).
- `include/RendererUtils.h` — helpers DRY compartidos.
- `include/Constants.h` — todas las constantes (dimensiones, rutas, VT).
- `src/CoarseRenderer.cpp` — scheduler CGMT con `sched_mutex`/`sched_cv`/`switch_to_next_thread()`.
- `src/SMTRenderer.cpp` — simulación pura de W=2 slots sin OS threads.
- `src/CMPRenderer.cpp` — N cores con `std::thread` independientes (rama `cmp`).

## Correctness
Todos los modelos deben producir imágenes PPM byte-exactas.
Verificar siempre con `diff results/image/frame_*.ppm`.

## Scripts
```bash
./scripts/run_all_models.sh          # flujo completo: 5 modelos + gráficas
./build/raytracer --model [sequential|fgmt|cgmt|smt|cmp]
```

## Convenciones de commits
Formato: `tipo(scope): descripcion corta en minusculas`
- **Una sola línea**, sin cuerpo multilínea, **máximo ~72 caracteres**.
- Tipos válidos: `feat`, `fix`, `refactor`, `chore`, `docs`, `test`.
- Scope = nombre del módulo afectado: `smt`, `cmp`, `fgmt`, `cgmt`, `scripts`, `main`.
- Ejemplos correctos del proyecto:
  ```
  feat(cmp): CMPRenderer con paralelismo real N=4 cores OS threads
  refactor(smt): simulacion de unidades funcionales, sin OS threads
  fix(smt): cambio a uso de semaforos por thread
  chore(scripts): dpi=300 en generar_graficas.py
  ```
- **NO** usar cuerpos de commit largos, listas con `-`, ni bloques de código en el mensaje.

## Flujo de ramas
```
main ← develop ← refactor   (mejoras a modelos existentes)
                ← cmp        (modelo CMP, desde develop)
                ← fgmt/cgmt  (ramas de modelo específico)
```
Merge de feature → develop antes de integrar a main.

## Nota sobre la interfaz de chat de Copilot
Cuando el agente propone un comando `git commit`, editar el mensaje en el
textarea del chat y confirmar con "Permitir" **puede mostrar el mensaje
original** del agente aunque se haya escrito uno distinto — es un bug visual
conocido de la UI. El comando que realmente ejecuta el shell es el de la
terminal (el texto del textarea se aplica en el input de la tool). Para
asegurarse de que el mensaje correcto queda, verificar con `git log --oneline -1`
después del commit.
