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
