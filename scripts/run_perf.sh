#!/usr/bin/env bash
# run_perf.sh — Perfilado con 'perf stat' para los modelos de ejecución.
#
# Captura contadores de hardware relevantes para el análisis de SMT/CMP:
#   cycles, instructions, cache-misses, cache-references, branches, branch-misses
#   task-clock, context-switches, cpu-migrations, page-faults
#
# Herramienta requerida por el enunciado (sección profiling):
#   Reference: https://perfwiki.github.io/main/
#
# Las métricas de hardware (ciclos, instrucciones, CPI, IPC, stalls) permiten
# correlacionar el comportamiento observado con los modelos de ejecución
# (fine-grained, coarse-grained, SMT, CMP).
#
# Uso:
#   ./scripts/run_perf.sh                           # todos los modelos
#   ./scripts/run_perf.sh sequential cmp smt        # solo esos modelos
#
# Resultados guardado en: results/perf/perf_<modelo>.txt
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
PERF_DIR="$PROJECT_ROOT/results/perf"

cd "$PROJECT_ROOT"
mkdir -p "$PERF_DIR"

# Modelos a perfilar
if [ $# -gt 0 ]; then
    MODELS=("$@")
else
    MODELS=(sequential fgmt cgmt smt cmp)
fi

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  PERFILADO CON perf stat — MODELOS DE EJECUCIÓN                 ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
echo "  Referencia: https://perfwiki.github.io/main/"
echo ""

# ── Verificar disponibilidad de perf ─────────────────────────────────────────
if ! command -v perf &> /dev/null; then
    echo "  ✗ 'perf' no está instalado."
    echo "    Instalar con:"
    echo "      sudo apt install linux-perf          # Debian / Ubuntu"
    echo "      sudo dnf install perf                # Fedora / RHEL"
    echo "      sudo pacman -S perf                  # Arch"
    exit 1
fi
PERF_VER=$(perf --version 2>&1 | head -1)
echo "  perf disponible : $PERF_VER"

# Verificar permiso de acceso a contadores hardware (paranoid level)
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "desconocido")
echo "  perf_event_paranoid : $PARANOID"
if [ "$PARANOID" -gt 1 ] 2>/dev/null; then
    echo "  ⚠ paranoid=$PARANOID — contadores hardware pueden estar restringidos."
    echo "    Para habilitar temporalmente:"
    echo "      echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid"
fi
echo "  Salida: $PERF_DIR/"
echo ""

# ── Eventos de hardware (microarquitectura y caché) ──────────────────────────
# Estos eventos están directamente relacionados con el análisis SMT/CMP:
#   cycles         → ciclos totales de CPU (para calcular CPI)
#   instructions   → instrucciones retiradas  (IPC = instructions/cycles)
#   cache-misses   → fallos de caché L1/LLC que generan stalls
#   cache-references → accesos a caché (hit ratio)
#   branches / branch-misses → análisis de predictibilidad del flujo
HW_EVENTS="cycles,instructions,cache-misses,cache-references,branches,branch-misses"

# Eventos de software (siempre disponibles, incluso en VM)
SW_EVENTS="task-clock,context-switches,cpu-migrations,page-faults"

# ── Perfilar cada modelo ──────────────────────────────────────────────────────
for model in "${MODELS[@]}"; do
    OUT_FILE="$PERF_DIR/perf_${model}.txt"
    echo "  [$model] Ejecutando perf stat..."

    {
        echo "=================================================================="
        echo "  PERF STAT — Modelo: ${model^^}"
        echo "  Fecha : $(date '+%Y-%m-%d %H:%M:%S')"
        echo "  Cmd   : $BUILD_DIR/raytracer --model $model"
        echo "=================================================================="
        echo ""
        echo "--- Contadores de hardware (ciclos, instrucciones, caché, ramas) ---"
        echo "    (ciclos/instrucciones → CPI / IPC)"
        echo "    (cache-misses → stalls de memoria que el scheduler intenta ocultar)"
        echo ""
    } > "$OUT_FILE"

    # Intentar con contadores hardware primero
    if perf stat \
        -e "$HW_EVENTS" \
        "$BUILD_DIR/raytracer" --model "$model" \
        >> "$OUT_FILE" 2>&1; then
        echo "    ✓ Contadores hardware OK"
    else
        echo "    ⚠ Contadores hardware no disponibles (normal en VM/contenedor)."
        echo "      Se usarán solo contadores de software."
        echo "(contadores hardware no disponibles en este entorno)" >> "$OUT_FILE"
    fi

    {
        echo ""
        echo "--- Contadores de software (task-clock, context-switches, page-faults) ---"
        echo "    (context-switches → cambios de contexto del SO durante la ejecución)"
        echo "    (cpu-migrations   → si el hilo migró entre CPUs físicos)"
        echo ""
    } >> "$OUT_FILE"

    perf stat \
        -e "$SW_EVENTS" \
        "$BUILD_DIR/raytracer" --model "$model" \
        >> "$OUT_FILE" 2>&1 || {
        echo "    ⚠ Contadores software también fallaron."
        echo "(contadores software no disponibles)" >> "$OUT_FILE"
    }

    {
        echo ""
        echo "--- Notas de interpretación ---"
        echo "  CPI = cycles / instructions"
        echo "  IPC = instructions / cycles  (mayor = mejor pipeline utilization)"
        echo "  Cache miss rate = cache-misses / cache-references * 100"
        echo "  FGMT/CGMT: stalls ocultados por rotación de contexto → menor CPI esperado"
        echo "  SMT       : 2 hilos lógicos comparten pipeline → ver IPC combinado"
        echo "  CMP       : 4 cores independientes → IPC por core similar a Sequential"
        echo ""
    } >> "$OUT_FILE"

    echo "    ✓ Guardado: $OUT_FILE"
done

# ── Resumen en consola de los archivos generados ──────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  ✓ PERFILADO COMPLETADO                                         ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
echo "  Archivos generados:"
for model in "${MODELS[@]}"; do
    f="$PERF_DIR/perf_${model}.txt"
    [ -f "$f" ] && echo "    $f"
done
echo ""
echo "  Métricas clave a reportar en el documento:"
echo "    - cycles / instructions → CPI  (baja en CGMT si stalls ocultos)"
echo "    - cache-misses          → tasa de miss L1/LLC"
echo "    - context-switches      → cuántos cambios de contexto del SO"
echo "    - cpu-migrations        → si los hilos migraron entre cores"
echo ""
echo "  Referencia oficial perf: https://perfwiki.github.io/main/"
echo ""
