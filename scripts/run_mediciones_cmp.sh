#!/usr/bin/env bash
# run_mediciones_cmp.sh — Ejecuta Sequential + CMP (200 frames de animación cada uno),
# verifica los CSVs y genera el reporte de speedup con gráficas.
#
# Uso: ./scripts/run_mediciones_cmp.sh
#
# CMP = Chip Multiprocessing: 4 cores con OS threads reales (std::thread).
# Cada core procesa su cuarto de la imagen de forma independiente (sin sync).
# VT = max(VT por core) — semántica de reloj de pared real.
# Timeouts: Sequential=60s, CMP=30s (paralelismo real).
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
SCRIPTS_DIR="$PROJECT_ROOT/scripts"
CSV_SEQ="$RESULTS_DIR/mediciones_secuencial.csv"
CSV_CMP="$RESULTS_DIR/mediciones_cmp.csv"
IMG_SEQ="$RESULTS_DIR/image/frame_secuencial.ppm"
IMG_CMP="$RESULTS_DIR/image/frame_cmp.ppm"
GRAPHS_DIR="$RESULTS_DIR/graficas"
LOG_FILE="$RESULTS_DIR/speedup_report_cmp.log"
EXPECTED_FRAMES=200

cd "$PROJECT_ROOT"

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  MEDICIONES COMPARATIVAS: SEQUENTIAL vs CMP (4 cores)         ║"
echo "║  Animación 200 frames · Órbita elíptica · 80×60 px            ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# ── 1. Compilar si es necesario ───────────────────────────────────────────────
echo "[1/4] Verificando compilación..."
if [ ! -x "$BUILD_DIR/raytracer" ]; then
    echo "    Compilando proyecto..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    cmake --build build/ > /dev/null 2>&1
fi
echo "    ✓ Ejecutable listo"

# ── Helper: verificar CSV ─────────────────────────────────────────────────────
check_csv() {
    local csv="$1" label="$2"
    local lines
    lines=$(wc -l < "$csv" 2>/dev/null || echo 0)
    local frames=$(( lines - 1 ))   # descontar header
    if [ "$frames" -ge "$EXPECTED_FRAMES" ]; then
        echo "    ✓ $label: $frames frames en CSV"
    else
        echo "    ✗ $label: solo $frames frames (se esperaban $EXPECTED_FRAMES)"
        exit 1
    fi
}

# ── 2. Sequential ─────────────────────────────────────────────────────────────
echo ""
echo "[2/4] Ejecutando Sequential (200 frames)..."
timeout 60 ./build/raytracer --model sequential > /tmp/seq_out.txt 2>&1 || {
    echo "    ✗ Sequential falló o excedió timeout (60s)"
    tail -10 /tmp/seq_out.txt
    exit 1
}
check_csv "$CSV_SEQ" "Sequential"

# ── 3. CMP (4 cores, paralelismo real) ───────────────────────────────────────
echo ""
echo "[3/4] Ejecutando CMP — 4 cores OS threads (200 frames)..."
timeout 30 ./build/raytracer --model cmp > /tmp/cmp_out.txt 2>&1 || {
    echo "    ✗ CMP falló o excedió timeout (30s)"
    tail -10 /tmp/cmp_out.txt
    exit 1
}
check_csv "$CSV_CMP" "CMP"

# ── 4. Validar imágenes y generar gráficas ────────────────────────────────────
echo ""
echo "[4/4] Validando imágenes y generando reporte de speedup..."

get_size() { [ -f "$1" ] && stat -c%s "$1" 2>/dev/null || echo 0; }
SIZE_SEQ=$(get_size "$IMG_SEQ")
SIZE_CMP=$(get_size "$IMG_CMP")

echo "    Sequential : $SIZE_SEQ bytes"
echo "    CMP        : $SIZE_CMP bytes"

if [ "$SIZE_SEQ" -gt 0 ] && [ "$SIZE_CMP" -gt 0 ]; then
    if diff -q "$IMG_SEQ" "$IMG_CMP" > /dev/null 2>&1; then
        echo "    ✓ Imágenes byte-exactas (correctness verificado)"
    else
        echo "    ✗ FALLO de correctness: las imágenes difieren"
        echo "      Sequential: $IMG_SEQ"
        echo "      CMP:        $IMG_CMP"
        exit 1
    fi
else
    echo "    ✗ Faltan imágenes para la validación"
    exit 1
fi

mkdir -p "$GRAPHS_DIR"
python3 "$SCRIPTS_DIR/analizar_speedup.py" \
    "$CSV_SEQ" "$CSV_CMP" \
    --graphs "$GRAPHS_DIR" --log "$LOG_FILE"

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  REPORTE FINALIZADO                                            ║"
echo "╠════════════════════════════════════════════════════════════════╣"
echo "║  CSV Sequential : $CSV_SEQ"
echo "║  CSV CMP        : $CSV_CMP"
echo "║  Log speedup    : $LOG_FILE"
echo "║  Gráficas       : $GRAPHS_DIR/"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
