# Rúbrica de Evaluación — A2 Proyecto Individual 1S2026
# Análisis de Estado Actual y Objetivos

---

## Escala de Calificación

| Puntos | Descripción |
|--------|-------------|
| 0 | No implementado o incorrecto conceptualmente |
| 1 | Implementación deficiente o evidencia mínima |
| 2 | Cumplimiento parcial, evidencia incompleta o errores técnicos relevantes |
| 3 | Cumple con detalles menores faltantes o pequeños errores no críticos |
| 4 | Cumple completamente, evidencia verificable, sin errores técnicos |

---

## Categorías de Evaluación

### 1. Implementación correcta de los 4 modelos — Peso 20.01%

**Descriptor de máxima nota (4 pts):**
Los 4 modelos están implementados correctamente, ejecutan el mismo problema base, producen
resultados válidos y diferenciables. Se demuestra ejecución en vivo.

#### Estado actual
- **5 modelos** implementados y funcionales: `sequential`, `fgmt`, `cgmt`, `smt`, `cmp`.
- Todos ejecutan el mismo ray tracer (80×60, 3 esferas, 200 frames de animación).
- Correctness verificado: las imágenes PPM de referencia son byte-exactas entre modelos.
- Ejecución mediante `./build/raytracer --model <nombre>` o via `run_all_models.sh`.
- El binario compilado ya existe en `build/raytracer`.

#### Brechas / riesgos
- La rúbrica menciona "4 modelos" pero el proyecto implementa 5 (el secuencial actúa como
  *baseline*, los 4 evaluables son FGMT, CGMT, SMT y CMP). Asegurarse de que en la demostración
  en vivo se demuestre el baseline y los 4 modelos concurrentes.
- Tener el entorno compilado antes de la evaluación para evitar tiempo perdido en build.

#### Objetivo para 4/4
- [ ] Demostración en vivo de los 5 modelos con salida visible por consola.
- [ ] Mostrar que las imágenes generadas son idénticas (`diff results/image/*.ppm`) para
  evidenciar correctness.

---

### 2. Corrección conceptual de cada modelo — Peso 13.33%

**Descriptor de máxima nota (4 pts):**
Fine-grained, coarse-grained, SMT y CMP implementados conforme a definición teórica.
No hay confusión conceptual.

#### Estado actual
| Modelo | Comportamiento esperado (teoría) | Implementación actual |
|--------|-----------------------------------|-----------------------|
| **Sequential** | 1 hilo, stall completo (`CACHE_MISS_PENALTY_NS = 3200 ns`), VT = suma acumulada | Correcto. CPI observado ≈ 12.73 |
| **FGMT** | 4 contextos, 1 pipeline, rota cada ciclo. Stall = 1 quantum perdido (`PIXEL_QUANTUM_NS`). Reduce CPI vs. sequential | Correcto. VT avg ≈ 5.2 ms (< 6.1 ms sequential) |
| **CGMT** | 4 contextos, 1 pipeline, rota *solo* en stall. Stall completamente oculto. CPI ideal = 10.0 | Correcto. CPI observado = 10.0 exacto |
| **SMT** | W=2 issue slots, 0 OS threads, simulación pura. VT = `global_clock × PIXEL_QUANTUM_NS` | Correcto. CPI ≈ 5.07 (≈ CGMT/2) |
| **CMP** | N=4 cores independientes con OS threads reales. VT = `max(VT por core)` | Correcto. VT avg ≈ 1.55 ms (menor al resto) |

#### Brechas / riesgos
- FGMT y CGMT comparten pipeline lógico → su VT es la *suma* de los 4 contextos.
  SMT y CMP reflejan reloj de pared real. Esta distinción debe quedar clara en la presentación.
- El CPI de SMT (≈ 5.07) debería ser ≈ CGMT/2 = 5.0; el valor actual es coherente pero
  no exacto; documentar por qué (variación por número de stalls por frame).

#### Objetivo para 4/4
- [ ] Preparar tabla comparativa de métricas por modelo para la defensa (CPI, VT, speedup).
- [ ] Explicar verbalmente la diferencia pipeline compartido vs. cores independientes.

---

### 3. Diseño experimental y control de variables — Peso 13.33%

**Descriptor de máxima nota (4 pts):**
SMT deshabilitado/habilitado correctamente, hardware físico comprobado, parámetros fijos,
control de carga del sistema documentado.

#### Estado actual
- Ejecuciones en **WSL2** (entorno virtualizado). Los contadores hardware de `perf` no están
  disponibles (`⚠ WSL2: contadores hardware omitidos`).
- Parámetros fijos: resolución 80×60, 3 esferas, 200 frames — definidos en `Constants.h`.
- 200 repeticiones por modelo, I/O excluido del timer (mide solo `render_frame()`).
- El script `run_smt_comparison.sh` existe para comparar SMT on/off (archivo
  `mediciones_cmp_smt_on.csv` presente en `results/`).

#### Brechas / riesgos (crítico)
- **Hardware físico**: La rúbrica exige "hardware físico comprobado". WSL2 es una VM sobre
  Windows → los contadores de ciclos, IPC, cache-miss del hardware real no son accesibles.
  Esto es la mayor brecha del proyecto.
- **SMT habilitado/deshabilitado**: El SMT del proyecto es un modelo simulado, no el
  Hyper-Threading físico del procesador. Para demostrar el control del HW SMT real se
  necesitaría ejecutar en Linux nativo y alternar `/sys/devices/system/cpu/smt/control`.
- **Control de carga del sistema**: No hay documentación de qué procesos del sistema estaban
  activos durante las mediciones.

#### Objetivo para máxima nota
- [ ] Ejecutar en metal desnudo (Linux nativo) con `perf` habilitado para obtener contadores
  hardware reales. Si no es posible, documentar explícitamente la limitación y compensar con
  análisis de los contadores de software disponibles.
- [ ] Documentar el procedimiento de "control de carga": CPU governor (performance), procesos
  de fondo detenidos, HT habilitado/deshabilitado en BIOS o via kernel.
- [ ] Agregar en el README o en un doc la evidencia de ejecución en hardware físico (hostname,
  `lscpu`, `uname -a`).

---

### 4. Medición estadística rigurosa — Peso 13.33%

**Descriptor de máxima nota (4 pts):**
Media, desviación estándar, intervalo 95%, boxplots/histogramas generados automáticamente.

#### Estado actual
| Métrica estadística | Estado |
|--------------------|--------|
| Media | ✓ Calculada y reportada por modelo |
| Desviación estándar | ✓ Reportada en los `.txt` de perf |
| Mín / Máx | ✓ Presentes en reportes |
| Intervalo de confianza 95% | ⚠ Verificar en `analizar_speedup.py` |
| Boxplots | ✓ `02_boxplot_comparativo.png` generado |
| Histogramas | ✓ `01_histogram_comparativo.png` generado |
| Automatización completa | ✓ `run_all_models.sh` genera todo |

Gráficas actuales en `results/graficas/`:
- `01_histogram_comparativo.png`
- `02_boxplot_comparativo.png`
- `03_speedup_comparison.png`
- `04_timeline_executions.png`
- `05_virtual_time_comparison.png`
- `06_cpu_speedup_smt_cmp.png`
- `07_efficiency.png`
- `08_scalability.png`
- `09_modeled_group_comparison.png`
- `10_real_group_comparison.png`

#### Brechas / riesgos
- Verificar que `analizar_speedup.py` calcula y reporta el **intervalo de confianza 95%**
  (IC95 = media ± 1.96 × σ/√n). Si no lo hace, es la única brecha.

#### Objetivo para 4/4
- [ ] Confirmar que el IC95% aparece en el reporte de speedup o en las gráficas.
- [ ] Si falta, agregar cálculo del IC en `analizar_speedup.py` y mostrarlo en el log/gráfica.

---

### 5. Análisis de perfilado (SMT y microarquitectura) — Peso 13.33%

**Descriptor de máxima nota (4 pts):**
Presenta métricas de ciclos, instrucciones, stalls y uso por hilo lógico; análisis técnico
coherente.

#### Estado actual
- Reportes de `perf stat` existen para los 5 modelos en `results/perf/`.
- En WSL2 solo están disponibles contadores de *software*:
  - `task-clock` (tiempo de CPU usado)
  - `context-switches` (cambios de contexto del SO)
  - `cpu-migrations` (migraciones entre CPUs físicos)
  - `page-faults` (fallos de página)
- **No disponibles en WSL2**: ciclos de CPU, instrucciones retiradas, IPC, cache-misses
  reales, branch mispredictions, stalls de pipeline hardware.
- Las métricas de stalls y CPI actuales son **simuladas** (calculadas por el modelo, no
  medidas por hardware).

#### Brechas / riesgos (crítico)
- La rúbrica pide "métricas de ciclos, instrucciones, stalls y uso por hilo lógico" — todas
  son hardware counters que WSL2 no provee.
- Sin hardware físico, se puede caer en la categoría 2/4 ("solo capturas sin análisis").

#### Objetivo para máxima nota
- [ ] Ejecutar en hardware físico Linux nativo con `perf stat -e cycles,instructions,
  cache-misses,cache-references,stalled-cycles-frontend,stalled-cycles-backend` para
  cada modelo.
- [ ] Si no es posible hardware físico, elaborar un análisis técnico detallado justificando
  cada métrica simulada (CPI calculado vs. CPI teórico) y su coherencia con la teoría de
  microarquitectura. Documentar la limitación del entorno explícitamente.
- [ ] Para SMT: mostrar la comparativa de usage de hilos lógicos (métricas `smt_2t_utilized`,
  o IPC por hilo lógico si el hardware lo soporta).

---

### 6. Análisis técnico vs teoría — Peso 13.33%

**Descriptor de máxima nota (4 pts):**
Explica comportamiento observado usando teoría de paralelismo, contención, overhead,
Amdahl implícito.

#### Estado actual
- Existe un log de speedup (`results/speedup_report.log`) generado automáticamente.
- No existe un documento de análisis técnico narrativo que conecte los resultados con la
  teoría de arquitecturas paralelas.

#### Brechas / riesgos
- Falta un documento (paper, informe o sección del README) que:
  1. Compare los speedups observados con la predicción teórica.
  2. Explique por qué CGMT oculta stalls completamente (CPI = 10) y FGMT no del todo.
  3. Justifique el speedup de CMP usando la Ley de Amdahl implícita.
  4. Analice el overhead de contención (SMT vs. CMP).
  5. Discuta las limitaciones del entorno WSL2 sobre las mediciones.

#### Objetivo para 4/4
- [ ] Redactar el análisis técnico (puede ser el paper IEEE de máx. 4 páginas requerido en
  `docs/instructions.md` o un informe dentro de `docs/`).
- [ ] Incluir tabla de speedup teórico vs. observado por modelo.
- [ ] Citar explícitamente: Ley de Amdahl, overhead de contención multihilo, ocultamiento
  de latencia en FGMT/CGMT, eficiencia paralela (E = S/N).

---

### 7. Calidad de código y arquitectura — Peso 6.67%

**Descriptor de máxima nota (4 pts):**
Código modular, documentado, reproducible, sin duplicación innecesaria.

#### Estado actual
- Arquitectura sigue principios SOLID:
  - `IRenderer`: interfaz abstracta (LSP, ISP)
  - `RendererFactory`: registry con `unordered_map` (OCP)
  - `GenericRunner`: orquestador que depende de `IRenderer*` (DIP)
- Helpers DRY en `RendererUtils.h` (`reset_thread_stats()`) y `Ray.h` (`make_ray()`).
- Constantes centralizadas en `Constants.h`.
- Agregar un modelo = 1 entrada en factory + constantes + source en CMake. Sin tocar
  `main.cpp` ni `GenericRunner`.
- Pipeline CI automatizado con `run_all_models.sh`.

#### Brechas / riesgos
- Verificar que las funciones no triviales tengan comentarios explicativos del *por qué*
  (no solo *qué* hacen).
- Asegurarse de que el build sea 100% reproducible desde un clone limpio.

#### Objetivo para 4/4
- [ ] Revisar `CoarseRenderer.cpp` y `SMTRenderer.cpp` para confirmar comentarios en lógica
  de scheduler.
- [ ] Verificar que `cmake .. && make -j4` funciona desde cero sin archivos preexistentes.

---

### 8. Uso correcto de Git — Peso 6.67%

**Descriptor de máxima nota (4 pts):**
Múltiples commits incrementales, uso correcto de ramas, tags finales, historial coherente.

#### Estado actual
| Aspecto | Estado |
|---------|--------|
| Commits incrementales | ✓ 20+ commits visibles, formato consistente |
| Ramas de feature | ✓ `secuencial`, `fgmt`, `cgmt`, `cmp`, `SMT`, `develop`, `refactor` |
| Flujo correcto (feature → develop → main) | ✓ Merge de CMP a develop presente |
| Tags finales | ✗ **Sin tags creados** (`git tag -l` vacío) |
| Historial coherente | ✓ Mensajes en formato `tipo(scope): descripcion` |

#### Brechas / riesgos
- **Ausencia de tags**: La rúbrica exige "tags finales". Se deben crear al menos un tag
  de release para el entregable final (ej. `v1.0.0` o `entrega-final`).
- La rama `refactor` es la HEAD actual pero `main` puede estar desactualizada — verificar
  que `main` contiene el estado final del proyecto antes de la entrega.

#### Objetivo para 4/4
- [ ] Mergear los cambios finales a `develop` y luego a `main`.
- [ ] Crear tag de entrega: `git tag -a v1.0.0 -m "entrega final A2 1S2026"`.
- [ ] Hacer push del tag: `git push origin v1.0.0`.

---

## Resumen de Estado y Prioridades

| # | Categoría | Peso | Estado estimado | Prioridad |
|---|-----------|------|-----------------|-----------|
| 1 | Implementación correcta de los 4 modelos | 20.01% | 4/4 ✓ | Baja (mantener) |
| 2 | Corrección conceptual de cada modelo | 13.33% | 4/4 ✓ | Baja (documentar) |
| 3 | Diseño experimental y control de variables | 13.33% | 2/4 ⚠ | **ALTA** |
| 4 | Medición estadística rigurosa | 13.33% | 3/4 ✓ | Media (IC95%) |
| 5 | Análisis de perfilado SMT y microarquitectura | 13.33% | 2/4 ⚠ | **ALTA** |
| 6 | Análisis técnico vs teoría | 13.33% | 2/4 ⚠ | **ALTA** |
| 7 | Calidad de código y arquitectura | 6.67% | 4/4 ✓ | Baja |
| 8 | Uso correcto de Git | 6.67% | 3/4 | Media (tags) |

---

## Plan de Acción por Prioridad

### Prioridad ALTA (pueden subir nota significativamente)

1. **Ejecutar en hardware físico** (categorías 3 y 5):
   - Compilar y ejecutar en Linux nativo (no WSL2) con `perf` habilitado.
   - Obtener contadores hardware: ciclos, instrucciones, cache-misses, stalls backend/frontend.
   - Guardar salida en `results/perf/` con nombre indicando "hardware_fisico".
   - Documentar en README: hostname, `lscpu`, `uname -a`, CPU governor = performance.

2. **Escribir el análisis técnico** (categoría 6):
   - Crear `docs/analisis_tecnico.md` con comparación teoría vs. resultados observados.
   - Incluir: Amdahl, eficiencia paralela, ocultamiento de latencia, overhead de contención.
   - Tabla speedup teórico (FGMT=4x, CGMT=4x ideal, SMT≈2x, CMP=4x) vs. speedup observado.

3. **Documentar control de variables** (categoría 3):
   - Agregar sección en README sobre el entorno de medición.
   - Procedimiento para deshabilitar/habilitar SMT hardware (HT en BIOS o
     `/sys/devices/system/cpu/smt/control`).

### Prioridad MEDIA

4. **Confirmar IC95% en estadísticas** (categoría 4):
   - Verificar que `analizar_speedup.py` imprime `[media - 1.96σ/√n, media + 1.96σ/√n]`.
   - Si falta, agregarlo y mostrar visualmente en alguna gráfica (barras de error).

5. **Tags de Git** (categoría 8):
   - `git tag -a v1.0.0 -m "entrega final A2 1S2026" && git push origin v1.0.0`

### Prioridad BAJA

6. **Demo en vivo documentada** (categoría 1):
   - Preparar guión de demostración mostrando los 5 modelos y el diff de imágenes.

7. **Comentarios en schedulers** (categoría 7):
   - Revisar comentarios en `CoarseRenderer.cpp`, `SMTRenderer.cpp`, `FinegrainedRenderer.cpp`.

---

## Objetivo Final del Sistema (estado ideal al momento de entrega)

El sistema debe cumplir los siguientes criterios verificables:

### Funcional
- Los 5 modelos compilan y ejecutan desde un clone limpio con
  `cmake .. && make -j4 && ./build/raytracer --model <nombre>`.
- Todos producen imágenes PPM byte-exactas (`diff results/image/*.ppm` = sin diferencias).
- El script `./scripts/run_all_models.sh` ejecuta todo el flujo sin intervención manual.

### Estadístico
- Cada modelo tiene ≥200 ejecuciones registradas en CSV.
- El reporte de speedup incluye: media, σ, mín, máx e **intervalo de confianza 95%**.
- Las 10 gráficas en `results/graficas/` se generan automáticamente.

### Experimental
- Mediciones realizadas en **hardware físico** (Linux nativo), no en VM/WSL2.
- Contadores hardware de `perf` disponibles: ciclos, instrucciones, cache-misses, stalls.
- Procedimiento de control de variables documentado (CPU governor, carga del sistema, HT).

### Analítico
- Documento `docs/analisis_tecnico.md` con análisis teórico vs. observado para cada modelo.
- Speedup observado coherente con la predicción teórica y las limitaciones de Amdahl.
- Sección explicando las limitaciones del entorno si se midió en WSL2.

### Git
- Rama `main` actualizada con el estado final del proyecto.
- Tag `v1.0.0` (o equivalente) creado y pusheado a `origin`.
- Historial limpio con el flujo `feature → develop → main`.
