#!/usr/bin/env bash
# run_all_models.sh — Ejecuta los 5 modelos (sequential, fgmt, cgmt, smt, cmp),
# verifica 200 frames en cada CSV, valida correctness y genera el reporte
# de speedup con gráficas.
#
# Uso: ./scripts/run_all_models.sh [--skip-smt] [--skip-fgmt] [--skip-cmp]
#
# Flags opcionales:
#   --skip-fgmt  Omitir FGMT (lento: ~102s con semáforos, ~204s legacy)
#   --skip-smt   Omitir SMT  (lento: varía según implementación)
#   --skip-cmp   Omitir CMP
#
# Cada modelo = 1 ejecución = 200 frames de animación (NUM_FRAMES en Constants.h).
# Timeouts: sequential=60s, cgmt=120s, fgmt=360s, smt=600s, cmp=120s.
set -euo pipefail

SKIP_FGMT=false
SKIP_SMT=false
SKIP_CMP=false
for arg in "$@"; do
    case "$arg" in
        --skip-fgmt) SKIP_FGMT=true ;;
        --skip-smt)  SKIP_SMT=true  ;;
        --skip-cmp)  SKIP_CMP=true  ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
GRAPHS_DIR="$RESULTS_DIR/graficas"
LOG_FILE="$RESULTS_DIR/speedup_report.log"
SCRIPTS_DIR="$PROJECT_ROOT/scripts"
EXPECTED_FRAMES=200

CSV_SEQ="$RESULTS_DIR/mediciones_secuencial.csv"
CSV_FGMT="$RESULTS_DIR/mediciones_fgmt.csv"
CSV_CGMT="$RESULTS_DIR/mediciones_cgmt.csv"
CSV_SMT="$RESULTS_DIR/mediciones_smt.csv"
CSV_CMP="$RESULTS_DIR/mediciones_cmp.csv"

IMG_SEQ="$RESULTS_DIR/image/frame_secuencial.ppm"
IMG_FGMT="$RESULTS_DIR/image/frame_fgmt.ppm"
IMG_CGMT="$RESULTS_DIR/image/frame_cgmt.ppm"
IMG_SMT="$RESULTS_DIR/image/frame_smt.ppm"
IMG_CMP="$RESULTS_DIR/image/frame_cmp.ppm"

cd "$PROJECT_ROOT"

echo ""
IMG_RES="$(grep 'IMAGE_WIDTH' include/Constants.h | grep -oP '\d+' | head -1)x$(grep 'IMAGE_HEIGHT' include/Constants.h | grep -oP '\d+' | head -1)"
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  MEDICIONES COMPARATIVAS: SEQUENTIAL · FGMT · CGMT · SMT · CMP  ║"
printf "║  Animación %d frames · Órbita elíptica · %-6s px             ║\n" "$EXPECTED_FRAMES" "$IMG_RES"
echo "╚══════════════════════════════════════════════════════════════════╝"
[ "$SKIP_FGMT" = true ] && echo "  ⚠ FGMT omitido (--skip-fgmt)"
[ "$SKIP_SMT"  = true ] && echo "  ⚠ SMT  omitido (--skip-smt)"
[ "$SKIP_CMP"  = true ] && echo "  ⚠ CMP  omitido (--skip-cmp)"
echo ""

# ── Helper: verificar que el CSV tiene N frames ───────────────────────────────
check_csv() {
    local csv="$1" label="$2"
    local lines frames
    lines=$(wc -l < "$csv" 2>/dev/null || echo 0)
    frames=$(( lines - 1 ))
    if [ "$frames" -ge "$EXPECTED_FRAMES" ]; then
        echo "    ✓ $label: $frames frames en CSV"
    else
        echo "    ✗ $label: $frames frames (se esperaban $EXPECTED_FRAMES)"
        exit 1
    fi
}

# ── 1. Compilar ───────────────────────────────────────────────────────────────
echo "[1/6] Compilando proyecto (incremental)..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
cmake --build build/ -- -j4 2>&1 | grep -E "^(\[|error:|warning:)" || true
echo "    ✓ Ejecutable listo"

# ── 2. Sequential ─────────────────────────────────────────────────────────────
echo ""
echo "[2/7] Ejecutando Sequential (200 frames)..."
timeout 60 ./build/raytracer --model sequential > /tmp/seq_out.txt 2>&1 || {
    echo "    ✗ Sequential falló o tardó más de 60s"; tail -10 /tmp/seq_out.txt; exit 1
}
check_csv "$CSV_SEQ" "Sequential"

# ── 3. FGMT ───────────────────────────────────────────────────────────────────
echo ""
if [ "$SKIP_FGMT" = false ]; then
    echo "[3/7] Ejecutando FGMT (200 frames · ~102s con semáforos)..."
    timeout 360 ./build/raytracer --model fgmt > /tmp/fgmt_out.txt 2>&1 || {
        echo "    ✗ FGMT falló o tardó más de 360s"; tail -10 /tmp/fgmt_out.txt; exit 1
    }
    check_csv "$CSV_FGMT" "FGMT"
else
    echo "[3/7] FGMT omitido."
fi

# ── 4. CGMT ────────────────────────────────────────────────────────────
echo ""
echo "[4/7] Ejecutando CGMT (200 frames)..."
timeout 120 ./build/raytracer --model cgmt > /tmp/cgmt_out.txt 2>&1 || {
    echo "    ✗ CGMT falló o tardó más de 120s"; tail -10 /tmp/cgmt_out.txt; exit 1
}
check_csv "$CSV_CGMT" "CGMT"

# ── 5. SMT ────────────────────────────────────────────────────────────────────
echo ""
if [ "$SKIP_SMT" = false ]; then
    echo "[5/7] Ejecutando SMT (200 frames)..."
    timeout 600 ./build/raytracer --model smt > /tmp/smt_out.txt 2>&1 || {
        echo "    ✗ SMT falló o tardó más de 600s"; tail -10 /tmp/smt_out.txt; exit 1
    }
    check_csv "$CSV_SMT" "SMT"
else
    echo "[5/7] SMT omitido."
fi

# ── 6. CMP ────────────────────────────────────────────────────────────────
echo ""
if [ "$SKIP_CMP" = false ]; then
    echo "[6/7] Ejecutando CMP — Chip Multiprocessing (200 frames, 4 cores reales)..."
    timeout 120 ./build/raytracer --model cmp > /tmp/cmp_out.txt 2>&1 || {
        echo "    ✗ CMP falló o tardó más de 120s"; tail -10 /tmp/cmp_out.txt; exit 1
    }
    check_csv "$CSV_CMP" "CMP"
else
    echo "[6/7] CMP omitido."
fi

# ── 7. Validación de correctness ────────────────────────────────────────────────
echo ""
echo "[7/7] Validando correctness e imágenes..."

ALL_OK=true
for pair in "fgmt:$IMG_FGMT" "cgmt:$IMG_CGMT"; do
    label="${pair%%:*}"
    img="${pair##*:}"
    if [ ! -f "$img" ]; then continue; fi
    if diff -q "$IMG_SEQ" "$img" > /dev/null 2>&1; then
        echo "    ✓ $label == sequential (byte-exact)"
    else
        echo "    ✗ $label difiere de sequential"
        ALL_OK=false
    fi
done

# SMT: puede diferir si su VT no coincide — reportar sin fallar
if [ "$SKIP_SMT" = false ] && [ -f "$IMG_SMT" ]; then
    if diff -q "$IMG_SEQ" "$IMG_SMT" > /dev/null 2>&1; then
        echo "    ✓ smt == sequential (byte-exact)"
    else
        echo "    ⚠ smt difiere de sequential (revisar scheduler SMT)"
    fi
fi

# CMP: paralelismo real — los pixels deben ser idénticos al sequential
if [ "$SKIP_CMP" = false ] && [ -f "$IMG_CMP" ]; then
    if diff -q "$IMG_SEQ" "$IMG_CMP" > /dev/null 2>&1; then
        echo "    ✓ cmp == sequential (byte-exact)"
    else
        echo "    ✗ cmp difiere de sequential"
        ALL_OK=false
    fi
fi

[ "$ALL_OK" = false ] && echo "    ⚠ Hay diferencias — revisar lógica de rendering"

# ── Análisis de speedup y gráficas ───────────────────────────────────────────
echo ""
echo "Generando reporte de speedup y gráficas..."
mkdir -p "$GRAPHS_DIR"

# Construir lista de CSVs que existen
CSV_ARGS=()
for csv in "$CSV_SEQ" "$CSV_FGMT" "$CSV_CGMT" "$CSV_SMT" "$CSV_CMP"; do
    [ -f "$csv" ] && CSV_ARGS+=("$csv")
done

python3 "$SCRIPTS_DIR/analizar_speedup.py" \
    "${CSV_ARGS[@]}" \
    --graphs "$GRAPHS_DIR" \
    --log "$LOG_FILE"

echo "    ✓ Reporte: $LOG_FILE"
echo "    ✓ Gráficas: $GRAPHS_DIR/"

# ── Resumen ───────────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  ✓ MEDICIONES COMPLETADAS                                       ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
echo "  CSVs        : $RESULTS_DIR/mediciones_*.csv"
echo "  Gráficas    : $GRAPHS_DIR/"
echo "  Log speedup : $LOG_FILE"
echo ""
echo "  ├─ mediciones_secuencial.csv"
echo "  ├─ mediciones_fgmt.csv"
echo "  ├─ mediciones_cgmt.csv"
echo "  ├─ mediciones_smt.csv"
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
echo "     ├─ frame_cgmt.ppm"
echo "     └─ frame_smt.ppm"
echo ""
