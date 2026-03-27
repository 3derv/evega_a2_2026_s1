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
SCRIPTS_DIR="$PROJECT_ROOT/scripts"

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

# ── Detectar entorno virtualizado (WSL / VM) ─────────────────────────────────
IS_WSL=false
if grep -qi microsoft /proc/version 2>/dev/null || grep -qi wsl /proc/sys/kernel/osrelease 2>/dev/null; then
    IS_WSL=true
    echo "  ⚠ Entorno WSL2 detectado."
    echo "    Los contadores de hardware (cycles, cache-misses) NO están disponibles en WSL."
    echo "    El enunciado requiere mediciones en hardware físico dedicado."
    echo "    Solo se usarán contadores de software (task-clock, context-switches)."
fi
echo ""

# ── Verificar disponibilidad de perf ─────────────────────────────────────────
PERF_REAL=""
# Buscar perf funcional para el kernel actual
for candidate in perf "perf_$(uname -r)" /usr/lib/linux-tools/*/perf; do
    if "$candidate" --version &>/dev/null 2>&1; then
        PERF_REAL="$candidate"
        break
    fi
done

if [ -z "$PERF_REAL" ]; then
    echo "  ✗ 'perf' no está disponible o no funciona para este kernel."
    echo "    Instalar con:"
    echo "      sudo apt install linux-tools-\$(uname -r) linux-tools-generic"
    echo ""
    echo "  Alternativa en WSL2: ejecutar en hardware físico (requerido por el enunciado)."
    exit 1
fi

# Probar que perf realmente funciona (puede existir pero no funcionar en WSL)
PERF_TEST=$(timeout 5 "$PERF_REAL" stat -e task-clock echo ok 2>&1 || true)
if echo "$PERF_TEST" | grep -q "not found\|Permission denied\|No such file"; then
    echo "  ✗ perf existe pero no funciona en este entorno."
    exit 1
fi

PERF_VER=$("$PERF_REAL" --version 2>&1 | head -1)
echo "  perf disponible : $PERF_VER"

# Verificar permiso de acceso a contadores hardware (paranoid level)
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "desconocido")
echo "  perf_event_paranoid : $PARANOID"
if [ "$PARANOID" -gt 1 ] 2>/dev/null && [ "$IS_WSL" = false ]; then
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
        [ "$IS_WSL" = true ] && echo "  Entorno: WSL2 — contadores hardware no disponibles"
        echo "=================================================================="
        echo ""
    } > "$OUT_FILE"

    # Intentar contadores hardware solo en hardware físico (no WSL)
    if [ "$IS_WSL" = false ]; then
        {
            echo "--- Contadores de hardware (ciclos, instrucciones, caché, ramas) ---"
            echo "    CPI = cycles/instructions  |  IPC = instructions/cycles"
            echo "    cache-miss rate = cache-misses/cache-references * 100"
            echo ""
        } >> "$OUT_FILE"
        if timeout 60 "$PERF_REAL" stat \
            -e "$HW_EVENTS" \
            -- "$BUILD_DIR/raytracer" --model "$model" \
            >> "$OUT_FILE" 2>&1; then
            echo "    ✓ Contadores hardware OK"
        else
            echo "    ⚠ Contadores hardware no disponibles."
            echo "(contadores hardware no disponibles)" >> "$OUT_FILE"
        fi
    else
        echo "    ⚠ WSL2: contadores hardware omitidos (requiere hardware físico)" | tee -a "$OUT_FILE"
    fi

    {
        echo ""
        echo "--- Contadores de software (siempre disponibles) ---"
        echo "    task-clock       → tiempo de CPU usado (ms)"
        echo "    context-switches → cambios de contexto del SO"
        echo "    cpu-migrations   → migraciones entre CPUs físicos"
        echo "    page-faults      → fallos de página"
        echo ""
    } >> "$OUT_FILE"

    if timeout 60 "$PERF_REAL" stat \
        -e "$SW_EVENTS" \
        -- "$BUILD_DIR/raytracer" --model "$model" \
        >> "$OUT_FILE" 2>&1; then
        echo "    ✓ Contadores software OK"
    else
        echo "    ⚠ Contadores software también fallaron."
        echo "(contadores software no disponibles)" >> "$OUT_FILE"
    fi

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
# ── Generar JSON estructurado desde los archivos .txt ────────────────────────
PYTHON_CMD=""
for py in python3 python; do
    if command -v "$py" &>/dev/null; then PYTHON_CMD="$py"; break; fi
done

if [ -n "$PYTHON_CMD" ]; then
    echo "  Generando JSON estructurado de métricas..."
    if "$PYTHON_CMD" "$SCRIPTS_DIR/perf_to_json.py" \
        --perf-dir "$PERF_DIR" \
        --output   "$PERF_DIR/perf_results.json" \
        --pretty; then
        echo "  ✓ JSON guardado en: $PERF_DIR/perf_results.json"
    else
        echo "  ⚠ Error al generar JSON (revisar perf_to_json.py)"
    fi
    echo ""
else
    echo "  ⚠ Python no encontrado — JSON no generado."
    echo ""
fi