#!/usr/bin/env bash
# run_smt_comparison.sh — Mide CMP con SMT (Hyper-Threading) activado y desactivado.
#
# Para aislar el efecto del SMT hardware en el rendimiento real:
#   - SMT ON  → los 4 cores CMP compiten con hilos lógicos del SO.
#   - SMT OFF → cada core físico trabaja sin compartir recursos con otro hilo lógico.
#
# Comando del profesor para desactivar SMT:
#   echo off | sudo tee /sys/devices/system/cpu/smt/control
#
# Requiere permisos sudo para escribir en el sysfs de CPU.
# No se admiten entornos virtualizados: las mediciones deben ser en hardware físico.
#
# Uso:
#   ./scripts/run_smt_comparison.sh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
GRAPHS_DIR="$RESULTS_DIR/graficas"
SCRIPTS_DIR="$PROJECT_ROOT/scripts"
SMT_CTRL="/sys/devices/system/cpu/smt/control"
EXPECTED_FRAMES=200

cd "$PROJECT_ROOT"

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  COMPARATIVA SMT ON vs OFF — Modelo CMP (4 cores reales)        ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

# ── Verificar que el ejecutable existe ───────────────────────────────────────
if [ ! -f "$BUILD_DIR/raytracer" ]; then
    echo "  ✗ Ejecutable no encontrado. Compilar primero con:"
    echo "    cmake -S . -B build && cmake --build build -j4"
    exit 1
fi

# ── Verificar disponibilidad del control SMT ─────────────────────────────────
SMT_AVAILABLE=false
if [ -f "$SMT_CTRL" ]; then
    SMT_AVAILABLE=true
    ORIGINAL_SMT=$(cat "$SMT_CTRL")
    echo "  SMT control disponible : $SMT_CTRL"
    echo "  Estado actual           : $ORIGINAL_SMT"
else
    echo "  ⚠ $SMT_CTRL no existe."
    echo "    Posibles causas:"
    echo "      - Entorno virtualizado (VM / contenedor / WSL) — no permitido para mediciones"
    echo "      - CPU sin soporte HT/SMT"
    echo "      - Kernel muy antiguo (< 4.14)"
    echo ""
    echo "  Se ejecutan mediciones BASE (estado natural del sistema) sin toggle."
fi
echo ""

# ── Helper: verificar CSV ─────────────────────────────────────────────────────
check_csv() {
    local csv="$1" label="$2"
    local frames
    frames=$(( $(wc -l < "$csv" 2>/dev/null || echo 0) - 1 ))
    if [ "$frames" -ge "$EXPECTED_FRAMES" ]; then
        echo "    ✓ $label: $frames frames"
    else
        echo "    ✗ $label: $frames frames (esperados $EXPECTED_FRAMES)"
        return 1
    fi
}

# ── Helper: guardar CSVs con sufijo ──────────────────────────────────────────
save_results() {
    local suffix="$1"
    local src dst
    src="$RESULTS_DIR/mediciones_cmp.csv"
    dst="$RESULTS_DIR/mediciones_cmp_${suffix}.csv"
    if [ -f "$src" ]; then
        cp "$src" "$dst"
        echo "    ✓ Guardado: $dst"
    fi
}

# ── Helper: ejecutar CMP ──────────────────────────────────────────────────────
run_cmp() {
    echo "  Ejecutando CMP (200 frames, 4 cores reales)..."
    timeout 120 "$BUILD_DIR/raytracer" --model cmp > /tmp/cmp_smt_out.txt 2>&1 || {
        echo "    ✗ CMP falló o tardó más de 120s"
        tail -5 /tmp/cmp_smt_out.txt
        return 1
    }
    echo "    ✓ CMP completado"
}

# ── Medición con SMT ON ───────────────────────────────────────────────────────
echo "[1/4] Midiendo con SMT ON..."
if [ "$SMT_AVAILABLE" = true ]; then
    echo on | sudo tee "$SMT_CTRL" > /dev/null
    sleep 0.5
    echo "      Estado SMT: $(cat "$SMT_CTRL")"
fi
run_cmp
check_csv "$RESULTS_DIR/mediciones_cmp.csv" "CMP SMT-ON"
save_results "smt_on"

# ── Desactivar SMT ────────────────────────────────────────────────────────────
if [ "$SMT_AVAILABLE" = true ]; then
    echo ""
    echo "[2/4] Desactivando SMT (Hyper-Threading)..."
    # Comando exacto indicado por el profesor:
    echo off | sudo tee "$SMT_CTRL" > /dev/null
    sleep 0.5
    echo "      Estado SMT: $(cat "$SMT_CTRL")"
    echo ""

    # ── Medición con SMT OFF ──────────────────────────────────────────────
    echo "[3/4] Midiendo con SMT OFF..."
    run_cmp
    check_csv "$RESULTS_DIR/mediciones_cmp.csv" "CMP SMT-OFF"
    save_results "smt_off"

    # ── Restaurar SMT ─────────────────────────────────────────────────────
    echo ""
    echo "[4/4] Restaurando SMT al estado original ($ORIGINAL_SMT)..."
    echo "$ORIGINAL_SMT" | sudo tee "$SMT_CTRL" > /dev/null
    sleep 0.3
    echo "      Estado SMT: $(cat "$SMT_CTRL")"

    # ── Comparativa numérica ──────────────────────────────────────────────
    ON_CSV="$RESULTS_DIR/mediciones_cmp_smt_on.csv"
    OFF_CSV="$RESULTS_DIR/mediciones_cmp_smt_off.csv"
    if [ -f "$ON_CSV" ] && [ -f "$OFF_CSV" ]; then
        avg_on=$(awk -F',' 'NR>1{s+=$2;c++}END{if(c>0)printf "%.6f",s/c}' "$ON_CSV")
        avg_off=$(awk -F',' 'NR>1{s+=$2;c++}END{if(c>0)printf "%.6f",s/c}' "$OFF_CSV")
        # speedup OFF vs ON (OFF suele ser más rápido si Hyper-Threading genera contención)
        speedup=$(awk "BEGIN{if($avg_on>0) printf \"%.4f\",$avg_on/$avg_off; else print \"N/A\"}")

        echo ""
        echo "══════════════════════════════════════════════════════════════════"
        echo "  RESULTADOS CMP: SMT ON vs SMT OFF"
        echo "══════════════════════════════════════════════════════════════════"
        echo "  Promedio CPU SMT ON  : ${avg_on} s/frame"
        echo "  Promedio CPU SMT OFF : ${avg_off} s/frame"
        echo "  Ratio OFF/ON         : ${speedup}x  (>1 → SMT OFF es más rápido)"
        echo "══════════════════════════════════════════════════════════════════"

        # Generar gráfica comparativa SMT ON vs OFF si matplotlib disponible
        if command -v python3 &> /dev/null; then
            echo ""
            echo "  Generando gráfica comparativa..."
            python3 - <<PYEOF
import sys, os
try:
    import pandas as pd
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError:
    print("    ⚠ pandas/matplotlib no disponibles. Omitiendo gráfica.")
    sys.exit(0)

on_csv  = "$ON_CSV"
off_csv = "$OFF_CSV"
graphs_dir = "$GRAPHS_DIR"
os.makedirs(graphs_dir, exist_ok=True)

on_data  = pd.read_csv(on_csv)
off_data = pd.read_csv(off_csv)

t_col = 'Tiempo(s)'
on_ms  = on_data[t_col].values * 1e3
off_ms = off_data[t_col].values * 1e3

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# Boxplot
bp = ax1.boxplot([on_ms, off_ms], labels=['CMP\nSMT ON', 'CMP\nSMT OFF'],
                 patch_artist=True, widths=0.5)
bp['boxes'][0].set_facecolor('darkorange'); bp['boxes'][0].set_alpha(0.75)
bp['boxes'][1].set_facecolor('steelblue');  bp['boxes'][1].set_alpha(0.75)
for med, val in zip(bp['medians'], [np.median(on_ms), np.median(off_ms)]):
    x = med.get_xdata().mean()
    ax1.text(x, val, f' {val:.3f}', va='center', fontsize=10, fontweight='bold')
ax1.set_ylabel('Tiempo CPU por frame (ms)', fontweight='bold')
ax1.set_title('Distribución CPU — SMT ON vs OFF\n(CMP · 4 cores reales)', fontweight='bold')
ax1.grid(True, alpha=0.3, axis='y')

# Barras de promedio con speedup
avgs = [on_ms.mean(), off_ms.mean()]
bars = ax2.bar(['SMT ON', 'SMT OFF'], avgs,
               color=['darkorange', 'steelblue'], alpha=0.8, edgecolor='black', width=0.4)
for bar, v in zip(bars, avgs):
    ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height(),
             f'{v:.3f} ms', ha='center', va='bottom', fontsize=11, fontweight='bold')
sp = avgs[0] / avgs[1] if avgs[1] > 0 else 0
ax2.set_ylabel('Tiempo CPU promedio por frame (ms)', fontweight='bold')
ax2.set_title(f'Promedio CPU — SMT ON vs OFF\nRatio ON/OFF = {sp:.3f}x', fontweight='bold')
ax2.set_ylim(0, max(avgs) * 1.3)
ax2.grid(True, alpha=0.3, axis='y')

plt.suptitle('Efecto del SMT (Hyper-Threading) en CMP — 4 cores reales',
             fontweight='bold', fontsize=13)
plt.tight_layout()
output = os.path.join(graphs_dir, '11_smt_on_vs_off.png')
plt.savefig(output, dpi=300, bbox_inches='tight')
plt.close()
print(f"    ✓ Gráfica guardada: {output}")
PYEOF
        fi
    fi
else
    echo ""
    echo "[2/4] SMT control no disponible — comparativa SMT ON/OFF omitida."
    echo "[3/4] Solo se tienen mediciones base."
    echo "[4/4] N/A"
fi

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║  ✓ COMPARATIVA SMT COMPLETADA                                   ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
echo "  Archivos generados:"
for f in \
    "$RESULTS_DIR/mediciones_cmp_smt_on.csv" \
    "$RESULTS_DIR/mediciones_cmp_smt_off.csv" \
    "$GRAPHS_DIR/11_smt_on_vs_off.png"; do
    [ -f "$f" ] && echo "    $f"
done
echo ""
echo "  Para controlar SMT manualmente:"
echo "    echo off | sudo tee /sys/devices/system/cpu/smt/control   # desactivar"
echo "    echo on  | sudo tee /sys/devices/system/cpu/smt/control   # activar"
echo "    cat /sys/devices/system/cpu/smt/control                   # ver estado"
echo ""
