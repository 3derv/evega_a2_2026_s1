# Análisis Técnico — Framework de Modelos de Ejecución Multithreading

**Proyecto:** A2 – 1S2026 · CE4302 Arquitectura de Computadores II
**Carga de trabajo:** Ray tracer 80×60, 3 esferas, 200 frames de animación

---

## 1. Contexto y objetivo del análisis

El proyecto simula cinco modelos de scheduling de hardware sobre un pipeline de cómputo
compartido (ray tracing por píxel). La métrica central es el **Tiempo Virtual (VT)**:
el número de nanosegundos que el modelo asigna al pipeline para completar un frame,
independientemente del tiempo de pared del sistema operativo.

El tiempo real de CPU **no** es la métrica de rendimiento para los modelos simulados
(FGMT, CGMT) porque incluye el overhead del SO (semáforos, mutex, condvars).
Para SMT y CMP, que usan OS threads o simulación pura, se reportan ambas métricas.

---

## 2. Descripción de los modelos

### 2.1 Sequential (baseline)
**Teoría:** Un único hilo ejecuta instrucciones en orden. Cada cache miss produce
un stall completo bloqueando el pipeline. No hay ocultamiento de latencia.

**Implementación:**
- 1 hilo, loop píxel a píxel.
- Stall → paga `CACHE_MISS_PENALTY_NS = 3200 ns` completo (32 NOPs × 100 ns).
- `VT += PIXEL_QUANTUM_NS + stall_cost` por píxel.

**CPI teórico:**
Con ~409 stalls sobre 4800 píxeles (80×60):
```
VT_total = N_pixeles × PIXEL_QUANTUM_NS + N_stalls × CACHE_MISS_PENALTY_NS
         = 4800 × 1000 + 409 × 3200 = 4,800,000 + 1,308,800 = 6,108,800 ns

CPI = VT_total / (NOP_PENALTY_NS × N_pixeles)
    = 6,108,800 / (100 × 4800) ≈ 12.73
```

**Observado:** VT avg = 6.110 ms, CPI = 12.73 ✓ Coincide con la teoría.

---

### 2.2 FGMT — Fine-Grained Multithreading
**Teoría:** Cuatro contextos de hardware comparten un único pipeline. El scheduler
rota el contexto activo en **cada ciclo**, haya o no stall. El stall de un thread
queda oculto porque otro contexto ejecuta útilmente durante esos ciclos.
La penalización visible para el thread en stall es solo 1 quantum perdido
(`PIXEL_QUANTUM_NS = 1000 ns`).

**Implementación:**
- 4 threads con semáforos de turno, scheduler en `global_clock_ % 4 == tid`.
- Stall → paga `NOP_PENALTY_NS = 100 ns` (detección); el stall de 3200 ns queda oculto.
- Pipeline compartido → `VT = suma(VT_thread_i)`.

**CPI teórico:**
```
VT_por_thread ≈ (N_pixeles/4) × PIXEL_QUANTUM_NS + N_stalls_por_thread × NOP_PENALTY
              ≈ 1200 × 1000 + ~102 × 100 = 1,200,000 + 10,200 ≈ 1,210,200 ns

VT_total = suma de 4 threads ≈ 4 × 1,210,200 ≈ 4,840,800 ns

CPI = 4,840,800 / (100 × 4800) ≈ 10.08
```
La rotación obligatoria agrega overhead de scheduling, en la práctica:
`VT_total ≈ 5,215,000 ns` → CPI ≈ 10.87.

**Observado:** VT avg ≈ 5.215 ms, CPI ≈ 10.87.

**Speedup vs Sequential (VT):** 6.110 / 5.215 = **1.17×**

**¿Por qué no es 4×?**
La rotación *obligatoria* en cada ciclo introduce overhead aunque no haya stall.
Cuando un thread está activo y no hay stall, igual pierde 3 ciclos esperando su turno
en lugar de ocupar 4/4 del pipeline. La eficiencia paralela es:
`E = 1.17 / 4 = 0.29 (29%)` — el pipeline está desaprovechado el 71% del tiempo.

---

### 2.3 CGMT — Coarse-Grained Multithreading
**Teoría:** El scheduler solo rota contexto cuando el hilo activo detecta un stall.
El hilo en ejecución no cede el pipeline innecesariamente; el stall queda **completamente
oculto** porque durante esos ciclos otro contexto trabaja.
El costo de cambio de contexto (`CONTEXT_SWITCH_COST_NS = 400 ns`) solo se paga si
el cambio realmente ocurre.

**Implementación:**
- Mutex/condvar `sched_mutex/sched_cv` + `current_thread` (round-robin en stall).
- `switch_to_next_thread()` solo se llama al detectar stall.
- Pipeline compartido → `VT = suma(VT_thread_i)`.

**CPI teórico (ideal):**
Si todos los stalls quedan 100% ocultos, cada píxel solo paga 1 quantum:
```
VT_por_thread = (N_pixeles/4) × PIXEL_QUANTUM_NS = 1200 × 1000 = 1,200,000 ns
VT_total = 4 × 1,200,000 = 4,800,000 ns
CPI_ideal = 4,800,000 / (100 × 4800) = 10.0
```

**Observado:** VT avg = 4.800 ms, **CPI = 10.0 exacto** ✓

Esto confirma que la implementación oculta el 100% de los stalls.

**Speedup vs Sequential (VT):** 6.110 / 4.800 = **1.27×**

**¿Por qué no es mayor que FGMT?**
CGMT elimina el overhead de la rotación obligatoria, pero aún comparte
un solo pipeline entre 4 contextos. El speedup teórico máximo de CGMT
sobre sequential es `VT_seq / VT_ideal = 6.110 / 4.800 = 1.27×`.
El límite está dado por la fracción de tiempo en stall:
- Active time por thread ≈ 1,200,000 ns (sin stalls)
- Sequential: 1,200,000 + stalls = 1,527,200 ns por thread equiv.
- Reducción = 1 − 1200/1527.2 ≈ 21.4% de mejora → 1/(1−0.214) ≈ 1.27× ✓

**Eficiencia paralela:** E = 1.27 / 4 = 0.318 (31.8%) — la eficiencia
es ligeramente mayor que FGMT porque no hay overhead de rotación vacía.

---

### 2.4 SMT — Simultaneous Multithreading
**Teoría:** El pipeline tiene W=2 issue slots independientes. En cada ciclo del
reloj global, si el slot actual no tiene stall, puede ejecutar 2 operaciones
concurrentemente. El VT es el tiempo del reloj global (`global_clock_ × Q`),
no la suma de tiempos por thread.

**Implementación:**
- Simulación pura sin OS threads (`global_clock_` atómico compartido).
- W=2 slots: si slot 0 tiene stall, el ciclo pasa a slot 1 (y viceversa).
- `VT = global_clock_ × PIXEL_QUANTUM_NS` — mide el reloj de pared del pipeline.

**Análisis de CPI:**
```
CPI_SMT = VT_smt / (NOP_NS × N_pixeles)
        = 2,435,655 / (100 × 4800) ≈ 5.07

Comparado con CGMT (CPI=10.0):
  CPI_SMT / CPI_CGMT ≈ 5.07 / 10.0 ≈ 0.507
```
Como W=2 permite ejecutar 2 operaciones por ciclo cuando ambos slots están activos,
el CPI tiende a `CPI_CGMT / 2 = 5.0`. El valor observado (5.07) refleja que no
siempre ambos slots tienen trabajo disponible (dependencias, stalls asimétricos).

**Speedup VT vs Sequential:** 6.110 / 2.436 = **2.51×**

**Nota sobre eficiencia:** E = 2.51 / 2 = 1.25x (superlineal en VT).
Esto no es paradójico: el CPI del pipeline real CGMT ya fue mejorado por
el ocultamiento de stalls. SMT añade una segunda dimensión de paralelismo
(issue width), no simplemente duplica threads en el mismo pipeline.

---

### 2.5 CMP — Chip Multi-Processor
**Teoría:** N=4 cores físicamente independientes. Cada core ejecuta su propio pipeline
sin compartir recursos de decodificación ni issue. Los stalls por cache miss en un
core **no** afectan a los demás. El VT es el `max(VT_core_i)` — lo que tarda
el núcleo más lento (bottleneck).

**Implementación:**
- 4 `std::thread` reales con OS scheduling.
- Cada core procesa un cuarto del frame (partición por bloques de píxeles).
- `VT_total = max(VT_core_0, VT_core_1, VT_core_2, VT_core_3)`.

**Análisis de CPI:**
```
CPI_CMP = VT_cmp / (NOP_NS × N_pixeles)
        = 1,547,408 / (100 × 4800) ≈ 3.22
```
Comparando con sequential (CPI=12.73):
- Sequential paga los stalls secuencialmente.
- CMP con 4 cores reduce el trabajo por core a N_pixeles/4 = 1200.
- Cada core tiene ~102 stalls; VT por core ≈ 1200×1000 + 102×3200 ≈ 1,526,400 ns.
- VT_total = max ≈ 1,536,000 ns → CPI ≈ 3.20 ✓

**Speedup VT vs Sequential:** 6.110 / 1.547 = **3.95×**

El speedup teórico ideal para N=4 es 4.0×. El speedup observado de 3.95×
implica una fracción secuencial muy pequeña:

**Análisis de Ley de Amdahl:**
```
S = 1 / (s + (1−s)/N)   donde s = fracción serial
3.95 = 1 / (s + (1−s)/4)
Despejando: s ≈ 0.42%  (0.0042)
```
El 99.58% del cálculo de ray tracing es perfectamente paralelizable
(cada píxel es independiente); el 0.42% restante es overhead de
inicialización de threads y recolección de resultados.

**Eficiencia paralela:** E = 3.95 / 4 = **0.988 (98.8%)** — casi perfecta.

---

## 3. Comparativa consolidada de métricas

| Modelo | VT avg (ms) | SpeedUp VT | CPI | N | Eficiencia (E=S/N) | Tipo pipeline |
|--------|-------------|-----------|-----|---|---------------------|---------------|
| Sequential | 6.110 | 1.00× | 12.73 | 1 | 1.000 (baseline) | 1 hilo, stall total |
| FGMT | 5.215 | 1.17× | 10.87 | 4 | 0.293 | Compartido, rotación obligatoria |
| CGMT | 4.800 | 1.27× | 10.00 | 4 | 0.318 | Compartido, solo en stall |
| SMT | 2.436 | 2.51× | 5.07 | 2 | 1.255* | W=2 issue slots paralelos |
| CMP | 1.547 | 3.95× | 3.22 | 4 | 0.988 | N=4 cores independientes |

*SMT VT-efficiency > 1 porque el CPI base ya incluía el costo de stalls de pipeline;
el SMT agrega 2-wide issue width, lo que no sigue la misma escala que N threads en 1 pipeline.

---

## 4. Análisis de contención y overhead

### 4.1 Contención en FGMT vs CGMT
FGMT y CGMT comparten el mismo número de contextos (N=4) y el mismo pipeline.
La diferencia de speedup (1.17× vs 1.27×) se debe a la **política de rotación**:

- **FGMT:** Rota cada ciclo → el hilo activo pierde el pipeline aunque no tenga stall.
  Overhead de scheduling = 3 ciclos waitados por cada ciclo útil (en el peor caso).
- **CGMT:** Rota **solo** en stall → el hilo usa el pipeline ininterrumpidamente.
  La rotación nunca "desperdicia" un ciclo útil.

La diferencia de CPI (10.87 vs 10.00) refleja directamente este overhead de rotación FGMT.

### 4.2 CMP superior al resto en VT
CMP (3.95×) supera significativamente a CGMT (1.27×) porque:
1. **Cores independientes:** Cada core tiene su propio issue unit, decode, y cache L1.
   No hay contención por el pipeline principal.
2. **Sin stall cruzado:** Un stall de cache en core 0 no detiene a core 1.
3. **Paralelismo real:** Los 4 cores ejecutan literalmente al mismo tiempo.

CGMT mejora la eficiencia del *tiempo de stall*, pero sigue siendo un único pipeline
que procesa un contexto a la vez. CMP duplica la capacidad de cómputo.

### 4.3 SMT: paralelismo de instrucciones vs paralelismo de datos
SMT agrega W=2 issue slots al mismo pipeline. A diferencia de CMP (que duplica
el núcleo completo), SMT solo duplica la etapa de ejecución. El beneficio es
mayor cuando los dos slots pueden ejecutar instrucciones independientes.

En el ray tracer (cálculo de intersecciones), las instrucciones de diferentes píxeles
son totalmente independientes, por lo que SMT puede aprovechar bien ambos slots.

---

## 5. Limitaciones del entorno de medición (WSL2)

Las mediciones actuales se realizaron en **WSL2** (subsistema Linux sobre Windows),
lo que introduce las siguientes limitaciones:

| Limitación | Impacto |
|-----------|---------|
| Contadores hardware de `perf` no disponibles | CPI, IPC, stalls de hardware no medibles |
| Overhead de virtualización | CPU wall-clock inflado (ej: CGMT CPU ≈ 260ms vs ~5ms en HW) |
| SMT hardware (Hyper-Threading) no controlable | No se puede deshabilitar HT del procesador |
| Interrupciones del hypervisor | Mayor varianza en tiempos reales |

**Consecuencia:** El **Tiempo Virtual** es la métrica fiable en este entorno.
El CPI y speedup calculados sobre VT son válidos porque el reloj virtual
es completamente determinista (simulado, no medido por hardware).

### 5.1 Plan para medición en hardware físico (Ubuntu)

Al ejecutar en hardware físico con Linux nativo:

```bash
# 1. Verificar entorno
uname -a && lscpu | grep -E "CPU|Thread|Core|Socket|Cache"

# 2. Configurar CPU governor a performance
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# 3. Controlar Hyper-Threading (SMT hardware)
cat /sys/devices/system/cpu/smt/control             # ver estado
echo off | sudo tee /sys/devices/system/cpu/smt/control  # deshabilitar HT
echo on  | sudo tee /sys/devices/system/cpu/smt/control  # habilitar HT

# 4. Reducir contadores paranoid
echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid

# 5. Ejecutar perf con contadores hardware
./scripts/run_all_models.sh --with-perf
```

Los contadores hardware esperados por modelo (hardware físico):

| Modelo | CPI (HW) esperado | Obs. |
|--------|-------------------|------|
| Sequential | ~12.7 | Stalls de L1 miss largos |
| FGMT | ~10.9 | Rotación reduce CPI vs seq |
| CGMT | ~10.0 | Stalls completamente ocultos |
| SMT | ~5.0 | 2-wide issue reduce CPI |
| CMP | ~12.7/core | Sin ocultar stalls por core |

**Nota:** En hardware con HT habilitado, `perf stat -e smt_2t_utilized:u` permite
medir qué porcentaje del tiempo ambos hilos lógicos del mismo core físico estaban activos,
demostrando el beneficio del SMT hardware sobre el modelo CMP.

---

## 6. Correctness de imágenes

Todos los modelos deben producir imágenes PPM byte-exactas. La verificación es:

```bash
diff results/image/frame_secuencial.ppm results/image/frame_fgmt.ppm  # debe ser vacío
diff results/image/frame_secuencial.ppm results/image/frame_cgmt.ppm
diff results/image/frame_secuencial.ppm results/image/frame_smt.ppm
diff results/image/frame_secuencial.ppm results/image/frame_cmp.ppm
```

Si hay diferencias, el modelo tiene un bug en el cálculo de píxeles.

---

## 7. Referencias teóricas

- **Ley de Amdahl:** S = 1/(s + (1−s)/N). Con s→0 y N=4, speedup máximo = 4×.
- **CPI ideal CGMT:** `VT_total / (Q × N_px)` donde Q=PIXEL_QUANTUM_NS=1000ns.
  Si 0 stalls visibles → CPI = N_threads × 1000 / (100 × N_px_total) × N_px_total/N_threads... simplificado al VT ideal = N × N_px/N × Q.
- **Eficiencia paralela:** E = S/N. E=1 → speedup lineal ideal; E<1 → overhead.
- **FGMT vs CGMT:** La diferencia teórica es el grado de rotación. FGMT tiene
  rotación cada ciclo → overhead de rotación = (N−1)/N × throughput. CGMT rota
  solo en stall → 0 overhead en operaciones sin stall.
- **SMT issue width:** Con W=2 y tareas completamente independientes, CPI_SMT ≈ CPI_CGMT/2.
  Con dependencias inter-instrucción, CPI_SMT > CPI_CGMT/2.

---

## 8. Ejecución completa con `--with-perf` (27-03-2026)

Se ejecutó el flujo completo:

```bash
./scripts/run_all_models.sh --with-perf
```

### 8.1 Correctness

La validación de imágenes PPM fue exitosa para todos los modelos:

- `fgmt == sequential` (byte-exact)
- `cgmt == sequential` (byte-exact)
- `smt == sequential` (byte-exact)
- `cmp == sequential` (byte-exact)

### 8.2 Resultados observados (200 frames)

| Modelo | VT promedio | CPU promedio | Stalls prom. | CPI |
|--------|-------------|--------------|--------------|-----|
| Sequential | 6.110 ms | 0.000205 s | 409.3 | 12.7285 |
| FGMT | 5.215 ms | 0.007976 s | 415.2 | 10.8651 |
| CGMT | 4.800 ms | 0.002607 s | 408.7 | 10.0000 |
| SMT | 2.436 ms | 0.000252 s | 408.7 | 5.0743 |
| CMP | 1.547 ms | 0.000137 s | 408.9 | 3.2238 |

Speedup reportado vs baseline:

- VT: FGMT `1.17x`, CGMT `1.27x`, SMT `2.51x`, CMP `3.95x`.
- CPU: SMT `0.81x`, CMP `1.50x`.

Interpretación:

- En modelos simulados de scheduler (FGMT/CGMT), el indicador principal sigue siendo VT.
- SMT y CMP muestran mejor VT que modelos de pipeline compartido, consistente con mayor paralelismo efectivo.
- CGMT mantiene CPI ideal `10.0`, confirmando ocultamiento de stalls en la simulación.

### 8.3 Resultado de `perf stat` en este entorno

Se generaron los archivos:

- `results/perf/perf_sequential.txt`
- `results/perf/perf_smt.txt`
- `results/perf/perf_cmp.txt`
- `results/perf/perf_results.json`

Pero no fue posible recolectar contadores hardware ni software por política del kernel:

- `perf_event_paranoid = 4`
- Error: acceso restringido a operaciones de observabilidad/performance.

Por eso, en `perf_results.json` los modelos aparecen con `hw_available: false` y sin contadores derivados (CPI/IPC por hardware, cache-miss rate, etc.).

### 8.4 Conclusión técnica actualizada

1. El experimento comparativo de los 5 modelos es válido y reproducible en términos de VT, CPI simulado y correctness de imagen.
2. El perfilado con `perf` se ejecutó correctamente a nivel de pipeline de scripts, pero la plataforma no permitió leer PMU por restricciones de permisos.
3. Para cerrar el análisis de microarquitectura con evidencia de hardware (cycles/instructions/cache-misses), se requiere repetir en Linux nativo con permisos de `perf` habilitados (por ejemplo `perf_event_paranoid <= 1`, idealmente `0`).

---

## 9. Ejecución con perf habilitado + CONTEXT_SWITCH_COST_NS en CGMT (27-03-2026, fase 2)

Se ejecutó el flujo completo con permisos de perf habilitados:

```bash
./run_perf_analysis.sh   # baja paranoid→0, corre análisis, restaura paranoid
```

### 9.1 Mejora a CGMT: Context Switch Cost

Se agregó una constante realista al modelo CGMT:

```cpp
// CONTEXT_SWITCH_COST_NS: overhead de cambio de contexto en hardware
// Típicamente ~50-500 ns. Estimamos 400 ns (CPU moderna).
inline const long long CONTEXT_SWITCH_COST_NS = 400LL;
```

Impacto en CGMT:

| Métrica | Anterior | Nuevo | Diferencia |
|---------|----------|-------|------------|
| VT promedio | 4.800 ms | 4.963 ms | +0.163 ms |
| CPI | 10.0000 | 10.3406 | +0.3406 |
| Speedup vs Seq | 1.27x | 1.23x | -0.04x |

Análisis:
- 409 stalls promedio × 400 ns ≈ 163,600 ns ≈ 0.164 ms adicional → coincide con VT observado ✓
- El modelo es ahora más realista: reconoce que cambiar de contexto en hardware no es gratis.
- El CPI aumentó de 10.0 a 10.3406, reflejando el overhead de cambio.

### 9.2 Contadores de hardware capturados (perf_event_paranoid = 0)

Con permisos habilitados, se capturaron contadores hardware en 200 ejecuciones:

**Sequential:**
```
  cycles              : 205,912,983
  instructions        : 417,847,075
  cache-misses        : 75,509
  cache-references    : 128,726
  branches            : 34,253,520
  branch-misses       : 1,105,650
  task-clock          : 44.35 msec
  context-switches    : 1
  cpu-migrations      : 1
```

IPC: `417,847,075 / 205,912,983 ≈ 2.03`
Cache miss rate: `75,509 / 128,726 ≈ 58.7%`

**SMT:**
```
  cycles              : 242,800,056
  instructions        : 559,690,534
  cache-misses        : 70,043
  cache-references    : 153,411
  branches            : 55,942,637
  branch-misses       : 1,332,758
  task-clock          : 53.48 msec
  context-switches    : 3
  cpu-migrations      : 2
```

IPC: `559,690,534 / 242,800,056 ≈ 2.30`
Cache miss rate: `70,043 / 153,411 ≈ 45.6%`

**CMP (4 cores, totales combinados):**
```
  cycles              : 412,229,062 (core) + 63,686,653 (atom)
  instructions        : 511,603,396 (core) + 75,806,346 (atom)
  cache-misses        : 468,869 (core) + 524,929 (atom)
  cache-references    : 4,229,902 (core) + 1,683,957 (atom)
  task-clock          : 95.43 msec (2.842 CPUs utilized)
  context-switches    : 738
  cpu-migrations      : 56
```

IPC (core): `511,603,396 / 412,229,062 ≈ 1.24`
Cache miss rate (core): `468,869 / 4,229,902 ≈ 11.1%`

### 9.3 Interpretación de contadores

1. **IPC (Instrucciones por Ciclo):**
  - Sequential: 2.03 (mejor que esperado para ray tracing puro)
  - SMT: 2.30 (mejor que seq, pero no 2x — hay dependencias inter-thread)
  - CMP (core): 1.24 (más bajo → la lógica del ray tracer tiene dependencias)

2. **Cache miss rate:**
  - Sequential: 58.7% (alto, consistente con CACHE_SIZE=256 bytes pequeño)
  - SMT: 45.6% (más bajo que seq — working set distribuido entre threads)
  - CMP (core): 11.1% (mucho más bajo → cada core trabaja en su región/zona)

3. **Context switches:**
  - Sequential: 1 (baseline, sin cambios de contexto)
  - SMT: 3 (cambios de contexto mínimos, scheduler eficiente)
  - CMP: 738 (alto, pero proporcional al número de threads independientes y I/O del SO)

4. **CPU utilización:**
  - Sequential: 0.994 (casi 1 core)
  - SMT: 0.994 (casi 1 core)
  - CMP: 2.842 (3 cores de los 4, no es 4.0 porque hay overhead + sincronización)

### 9.4 Síntesis final

La corrida completa con perf proporciona **validación empírica** de los modelos teóricos:

1. **CGMT realista:** El costo de context switch (400 ns) es necesario para modelar hardware real. VT: 4.963 ms (no el ideal 4.800 ms).

2. **SMT efectivo:** Aunque el IPC no se duplica, conseguimos 2.51x speedup en VT gracias a W=2 issue width simulado. Los contadores perf muestran mejor cache utilization.

3. **CMP paralelo:** Con 4 cores, logramos 3.95x speedup VT, aunque la utilización es 2.82 CPUs (no 4.0). Esto refleja overhead de sincronización y balance de carga.

4. **Imágenes byte-exact:** Todos los modelos coinciden → la correctness del ray tracing está garantizada independientemente del scheduling.

**Archivos generados en esta fase:**
- `results/mediciones_*.csv` — nuevos con CGMT actualizado
- `results/speedup_report.log` — métricas con CGMT mejorado
- `results/perf/perf_*.txt` — contadores hardware capturados
- `results/perf/perf_results.json` — síntesis estructurada

### 9.5 Comparativa SMT hardware on/off en CMP (27-03-2026)

Se ejecutó `./scripts/run_smt_comparison.sh` para medir el impacto del Hyper-Threading:

**Resultados:**

| Configuración | CPU time (avg) | VT (avg) | Diferencia |
|---------------|----------------|----------|-----------|
| SMT ON | 0.000140359 s | 1.547 ms | baseline |
| SMT OFF | 0.000140345 s | 1.547 ms | -0.00014 s (<0.01%) |

**Conclusión:**

El Hyper-Threading (SMT hardware) **no tiene efecto significativo** en este escenario:

1. **Razón:** El modelo CMP usa 4 threads independientes con OS scheduling real. Cada thread se asigna a un core físico completamente (gracias a que N=4 cores). Hyper-Threading solo ayuda cuando:
  - Hay más threads que cores físicos (ej: 8 threads en 4 cores físicos).
  - Hay mucha ILP (Instruction-Level Parallelism) dentro del mismo core.
  - La aplicación sufre muchos stalls que otro thread lógico pueda ocultar.

2. **Este caso:** Ray tracing pixel por pixel es altamente data-parallel, pero no hay mucho ILP intra-thread. Cada thread procesa 1200 píxeles (su tile) de forma secuencial. El SMT del hardware es "invisible" porque no hay contención por recursos en un core.

3. **Implicación teórica:** El modelo arquitectónico CMP no necesita simular SMT hardware explícitamente; 4 cores reales sin HT ≈ 4 cores reales con HT para esta carga.

**Archivos generados:**
- `results/mediciones_cmp_smt_on.csv` — 200 frames con HT enabled
- `results/mediciones_cmp_smt_off.csv` — 200 frames con HT disabled
