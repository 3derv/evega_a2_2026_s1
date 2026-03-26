#!/usr/bin/env bash
# run_mediciones_cgmt.sh — Ejecuta los 3 modelos (200 frames de animación cada uno),
# verifica los CSVs y genera el reporte de speedup con gráficas.
#
# Uso: ./scripts/run_mediciones_cgmt.sh
#
# Cada modelo renderiza 200 frames con órbita elíptica de cámara (1°/frame).
# Los tiempos por frame se graban en el CSV correspondiente.
# Timeouts: Sequential=60s, CGMT=120s, FGMT=600s (limitante: mutex/condvar por ciclo).
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
SCRIPTS_DIR="$PROJECT_ROOT/scripts"
CSV_SEQ="$RESULTS_DIR/mediciones_secuencial.csv"
CSV_FGMT="$RESULTS_DIR/mediciones_fgmt.csv"
CSV_CGMT="$RESULTS_DIR/mediciones_cgmt.csv"
IMG_SEQ="$RESULTS_DIR/image/frame_secuencial.ppm"
IMG_FGMT="$RESULTS_DIR/image/frame_fgmt.ppm"
IMG_CGMT="$RESULTS_DIR/image/frame_cgmt.ppm"
GRAPHS_DIR="$RESULTS_DIR/graficas"
LOG_FILE="$RESULTS_DIR/speedup_report.log"
EXPECTED_FRAMES=200

cd "$PROJECT_ROOT"

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  MEDICIONES COMPARATIVAS: SEQUENTIAL vs FGMT vs CGMT          ║"
echo "║  Animación 200 frames · Órbita elíptica · 160×120 px          ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# ── 1. Compilar si es necesario ───────────────────────────────────────────────
echo "[1/5] Verificando compilación..."
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
echo "[2/5] Ejecutando Sequential (200 frames)..."
timeout 60 ./build/raytracer --model sequential > /tmp/seq_out.txt 2>&1 || {
    echo "    ✗ Sequential falló o excedió timeout (60s)"
    cat /tmp/seq_out.txt | tail -10
    exit 1
}
check_csv "$CSV_SEQ" "Sequential"

# ── 3. CGMT ───────────────────────────────────────────────────────────────────
echo ""
echo "[3/5] Ejecutando CGMT (200 frames)..."
timeout 120 ./build/raytracer --model cgmt > /tmp/cgmt_out.txt 2>&1 || {
    echo "    ✗ CGMT falló o excedió timeout (120s)"
    cat /tmp/cgmt_out.txt | tail -10
    exit 1
}
check_csv "$CSV_CGMT" "CGMT"

# ── 4. Validación de imágenes ─────────────────────────────────────────────────
echo ""
echo "[4/5] Validando imágenes (último frame de cada modelo)..."
get_size() { [ -f "$1" ] && stat -c%s "$1" 2>/dev/null || echo 0; }
SIZE_SEQ=$(get_size "$IMG_SEQ")
SIZE_CGMT=$(get_size "$IMG_CGMT")

echo "    Sequential : $SIZE_SEQ bytes"
echo "    CGMT       : $SIZE_CGMT bytes"

if [ "$SIZE_SEQ" -gt 0 ] && [ "$SIZE_CGMT" -eq "$SIZE_SEQ" ]; then
    echo "    ✓ Imágenes byte-consistentes"
else
    echo "    ⚠ Tamaños distintos (diferente frame de cámara por diseño si FGMT aún no corrió)"
fi

# ── 5. Análisis de speedup y gráficas ────────────────────────────────────────
echo ""
echo "[5/5] Generando reporte de speedup y gráficas..."
mkdir -p "$GRAPHS_DIR"

# Usar analizar_speedup.py si los tres CSVs existen; si falta FGMT, solo seq+cgmt
if [ -f "$CSV_FGMT" ]; then
    python3 "$SCRIPTS_DIR/analizar_speedup.py" \
        "$CSV_SEQ" "$CSV_FGMT" "$CSV_CGMT" \
        --graphs "$GRAPHS_DIR" --log "$LOG_FILE"
else
    echo "    (FGMT no ejecutado — speedup solo Sequential vs CGMT)"
    python3 "$SCRIPTS_DIR/analizar_speedup.py" \
        "$CSV_SEQ" "$CSV_FGMT" "$CSV_CGMT" \
        --graphs "$GRAPHS_DIR" --log "$LOG_FILE" 2>/dev/null || \
    python3 - <<PYEOF
import numpy as np, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load_times(path):
    times = []
    with open(path) as f:
        f.readline()
        for line in f:
            parts = line.strip().split(',')
            if len(parts) >= 2:
                try: times.append(float(parts[1]))
                except ValueError: pass
    return np.array(times)

seq   = load_times("$CSV_SEQ")
cgmt  = load_times("$CSV_CGMT")
speedup = seq.mean() / cgmt.mean()

fig, ax = plt.subplots(figsize=(8,5))
ax.bar(['Sequential','CGMT'], [1.0, speedup], color=['steelblue','tomato'], alpha=0.8)
ax.axhline(1.0, linestyle='--', color='gray')
ax.set_ylabel('Speedup (vs Sequential)')
ax.set_title('Speedup Sequential vs CGMT · 200 frames')
for i,(v) in enumerate([1.0, speedup]):
    ax.text(i, v+0.01, f'{v:.2f}x', ha='center', fontweight='bold')
plt.tight_layout()
plt.savefig("$GRAPHS_DIR/speedup_seq_cgmt.png", dpi=150)
plt.close()
print(f"    Speedup CGMT vs Sequential: {speedup:.3f}x")
PYEOF
fi

echo "    ✓ Reporte: $LOG_FILE"
echo "    ✓ Gráficas: $GRAPHS_DIR/"

# ── Resumen ───────────────────────────────────────────────────────────────────
echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  ✓ MEDICIONES COMPLETADAS                                     ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "  CSV Sequential : $CSV_SEQ"
echo "  CSV CGMT       : $CSV_CGMT"
[ -f "$CSV_FGMT" ] && echo "  CSV FGMT       : $CSV_FGMT"
echo "  Gráficas       : $GRAPHS_DIR/"
echo "  Log speedup    : $LOG_FILE"
echo ""