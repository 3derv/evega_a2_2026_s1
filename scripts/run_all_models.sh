#!/bin/bash

# Script unificado: ejecuta los tres modelos (sequential, fgmt, cgmt),
# valida correctness y genera análisis comparativo de speed up.
# Uso: ./scripts/run_all_models.sh [NUM_RUNS]

set -e

NUM_RUNS="${1:-200}"
PROJECT_ROOT="/home/ederv/tec/p1arqui2/evega_a2_2026_s1"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
GRAPHS_DIR="$RESULTS_DIR/graficas"
LOG_FILE="$RESULTS_DIR/speedup_report.log"

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  MEDICIONES COMPARATIVAS: SEQUENTIAL vs FGMT vs CGMT            ║"
echo "║  Runs: $NUM_RUNS                                                  ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# ─── 1. Compilar ─────────────────────────────────────────────────────────────
echo "[1/6] Compilando proyecto (make incremental)..."
cd "$PROJECT_ROOT"
mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
cmake .. > /dev/null 2>&1
# Siempre ejecutar make para detectar cambios en headers o fuentes nuevas.
# Un binario stale (mezcla de objetos viejos y nuevos) puede causar crashes
# intermitentes que no aparecen con sanitizers pero sí en producción con -O2.
make -j4 2>&1 | grep -E "^(\[|error:|warning:)" || true
cd "$PROJECT_ROOT"
echo "    ✓ Compilación lista"

# ─── 2. Sequential ───────────────────────────────────────────────────────────
echo ""
echo "[2/6] Ejecutando $NUM_RUNS mediciones SEQUENTIAL..."
timeout 600 ./build/raytracer --model sequential --runs "$NUM_RUNS" > /tmp/seq_output.txt 2>&1
echo "    ✓ Sequential completado"

# ─── 3. FGMT ─────────────────────────────────────────────────────────────────
echo ""
echo "[3/6] Ejecutando $NUM_RUNS mediciones FGMT..."
timeout 600 ./build/raytracer --model fgmt --runs "$NUM_RUNS" > /tmp/fgmt_output.txt 2>&1
echo "    ✓ FGMT completado"

# ─── 4. CGMT ─────────────────────────────────────────────────────────────────
echo ""
echo "[4/6] Ejecutando $NUM_RUNS mediciones CGMT..."
timeout 600 ./build/raytracer --model cgmt --runs "$NUM_RUNS" > /tmp/cgmt_output.txt 2>&1
echo "    ✓ CGMT completado"

# ─── 5. Validación de correctness ────────────────────────────────────────────
echo ""
echo "[5/6] Validando correctness (diff de imágenes)..."

IMG_SEQ="$RESULTS_DIR/image/frame_secuencial.ppm"
IMG_FGMT="$RESULTS_DIR/image/frame_fgmt.ppm"
IMG_CGMT="$RESULTS_DIR/image/frame_cgmt.ppm"

ALL_OK=true

for IMG in "$IMG_FGMT" "$IMG_CGMT"; do
    MODEL_NAME=$(basename "$IMG" .ppm | sed 's/frame_//')
    if diff -q "$IMG_SEQ" "$IMG" > /dev/null 2>&1; then
        echo "    ✓ $MODEL_NAME == sequential (byte-exact)"
    else
        echo "    ✗ $MODEL_NAME difiere de sequential"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo ""
    echo "    ADVERTENCIA: Hay diferencias en las imágenes generadas."
    echo "    Los speed ups pueden no ser comparables."
fi

# ─── 6. Análisis de speed up y gráficas ──────────────────────────────────────
echo ""
echo "[6/6] Generando análisis de speed up y gráficas..."

mkdir -p "$GRAPHS_DIR"

if command -v python3 &> /dev/null; then
    python3 "$PROJECT_ROOT/scripts/analizar_speedup.py" \
        "$RESULTS_DIR/mediciones_secuencial.csv" \
        "$RESULTS_DIR/mediciones_fgmt.csv" \
        "$RESULTS_DIR/mediciones_cgmt.csv" \
        --graphs "$GRAPHS_DIR" \
        --log "$LOG_FILE"
    echo "    ✓ Análisis guardado en: $LOG_FILE"
    echo "    ✓ Gráficas guardadas en: $GRAPHS_DIR"
else
    echo "    ⚠ Python3 no disponible. Omitiendo análisis y gráficas."
fi

# ─── Resumen final ────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  ✓ MEDICIONES COMPLETADAS                                       ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
echo "Resultados en: $RESULTS_DIR"
echo "  ├─ mediciones_secuencial.csv"
echo "  ├─ mediciones_fgmt.csv"
echo "  ├─ mediciones_cgmt.csv"
echo "  ├─ speedup_report.log"
echo "  ├─ graficas/"
echo "  │  ├─ 01_histogram_comparativo.png"
echo "  │  ├─ 02_boxplot_comparativo.png"
echo "  │  ├─ 03_speedup_comparison.png"
echo "  │  ├─ 04_timeline_executions.png"
echo "  │  └─ 05_virtual_time_comparison.png"
echo "  └─ image/"
echo "     ├─ frame_secuencial.ppm"
echo "     ├─ frame_fgmt.ppm"
echo "     └─ frame_cgmt.ppm"
echo ""
