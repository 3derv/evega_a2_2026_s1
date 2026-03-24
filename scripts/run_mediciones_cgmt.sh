#!/bin/bash

# Script para ejecutar mediciones comparativas: SEQUENTIAL vs FGMT vs CGMT
# Uso: ./scripts/run_mediciones_cgmt.sh

set -e  # Exit on error

NUM_RUNS=200
PROJECT_ROOT="/home/ederv/tec/p1arqui2/evega_a2_2026_s1"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
CSV_FILE_CGMT="$RESULTS_DIR/mediciones_cgmt.csv"
CSV_FILE_FGMT="$RESULTS_DIR/mediciones_fgmt.csv"
CSV_FILE_SEQ="$RESULTS_DIR/mediciones_secuencial.csv"
IMG_CGMT="$RESULTS_DIR/image/frame_cgmt.ppm"
IMG_FGMT="$RESULTS_DIR/image/frame_fgmt.ppm"
IMG_SEQ="$RESULTS_DIR/image/frame_secuencial.ppm"
GRAPHS_DIR="$RESULTS_DIR/graficas"

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  MEDICIONES COMPARATIVAS: SEQUENTIAL vs FGMT vs CGMT          ║"
echo "║  Coarse-Grained Multithreading (Round-Robin en STALLS)        ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# 1. Compilar si es necesario
echo "[1/6] Verificando compilación..."
if [ ! -x "$BUILD_DIR/raytracer" ]; then
    echo "    ⚙ Compilando proyecto..."
    cd "$PROJECT_ROOT"
    mkdir -p build && cd build
    cmake .. > /dev/null 2>&1
    make > /dev/null 2>&1
    cd "$PROJECT_ROOT"
    echo "    ✓ Compilación completada"
else
    echo "    ✓ Ejecutable ya existe"
fi

# 2. Ejecutar mediciones SEQUENTIAL (200 runs)
echo ""
echo "[2/6] Ejecutando $NUM_RUNS mediciones SEQUENTIAL (línea base)..."
cd "$PROJECT_ROOT"
timeout 300 ./build/raytracer --model sequential --runs $NUM_RUNS > /tmp/seq_output.txt 2>&1
if [ $? -eq 124 ]; then
    echo "    ✗ TIMEOUT en SEQUENTIAL"
    exit 1
fi
echo "    ✓ Sequential completado"

# 3. Ejecutar mediciones FGMT (200 runs)
echo ""
echo "[3/6] Ejecutando $NUM_RUNS mediciones FGMT (Fine-Grained)..."
cd "$PROJECT_ROOT"
timeout 300 ./build/raytracer --model fgmt --runs $NUM_RUNS > /tmp/fgmt_output.txt 2>&1
if [ $? -eq 124 ]; then
    echo "    ✗ TIMEOUT en FGMT"
    exit 1
fi
echo "    ✓ FGMT completado"

# 4. Ejecutar mediciones CGMT (200 runs)
echo ""
echo "[4/6] Ejecutando $NUM_RUNS mediciones CGMT (Coarse-Grained)..."
cd "$PROJECT_ROOT"

# IMPORTANTE: No redireccionar stderr para CGMT, permitir que salga
# Usar timeout más generoso para CGMT
timeout 600 ./build/raytracer --model cgmt --runs $NUM_RUNS 2>&1 > /tmp/cgmt_output.txt
CGMT_EXIT=$?

if [ $CGMT_EXIT -eq 124 ]; then
    echo "    ✗ TIMEOUT en CGMT después de 600 segundos"
    echo "    Verifique si hay deadlock en la sincronización"
    exit 1
elif [ $CGMT_EXIT -ne 0 ]; then
    echo "    ✗ Error en CGMT (código: $CGMT_EXIT)"
    cat /tmp/cgmt_output.txt | tail -20
    exit 1
fi

echo "    ✓ CGMT completado"

# 5. Validación: Comparar imágenes generadas
echo ""
echo "[5/6] Validando correctness (comparando imágenes)..."

# Función para obtener tamaño de archivo
get_file_size() {
    if [ -f "$1" ]; then
        stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null || echo "0"
    else
        echo "0"
    fi
}

SIZE_SEQ=$(get_file_size "$IMG_SEQ")
SIZE_FGMT=$(get_file_size "$IMG_FGMT")
SIZE_CGMT=$(get_file_size "$IMG_CGMT")

echo "    Tamaños de imagen:"
echo "      - Sequential: $SIZE_SEQ bytes"
echo "      - FGMT:       $SIZE_FGMT bytes"
echo "      - CGMT:       $SIZE_CGMT bytes"

if [ "$SIZE_SEQ" -gt 0 ] && [ "$SIZE_FGMT" -eq "$SIZE_SEQ" ] && [ "$SIZE_CGMT" -eq "$SIZE_SEQ" ]; then
    echo "    ✓ Todas las imágenes tienen tamaño consistente"
else
    echo "    ⚠ Advertencia: Tamaños de imagen diferentes"
fi

# 6. Generar gráficas y análisis
echo ""
echo "[6/6] Generando gráficas y análisis de speed up..."

# Crear directorio de gráficas si no existe
mkdir -p "$GRAPHS_DIR"

# Generar gráficas comparativas usando Python (si está disponible)
if command -v python3 &> /dev/null; then
    
    # Script Python inline para generar gráficas
    python3 << 'PYTHON_SCRIPT'
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

RESULTS_DIR = "/home/ederv/tec/p1arqui2/evega_a2_2026_s1/results"
GRAPHS_DIR = f"{RESULTS_DIR}/graficas"
os.makedirs(GRAPHS_DIR, exist_ok=True)

try:
    seq_data = pd.read_csv(f"{RESULTS_DIR}/mediciones_secuencial.csv")
    fgmt_data = pd.read_csv(f"{RESULTS_DIR}/mediciones_fgmt.csv")
    cgmt_data = pd.read_csv(f"{RESULTS_DIR}/mediciones_cgmt.csv")
    
    seq_times = seq_data['Tiempo(s)'].values
    fgmt_times = fgmt_data['Tiempo(s)'].values
    cgmt_times = cgmt_data['Tiempo(s)'].values
    
    # =====================================================================
    # 1. Histograma comparativo
    # =====================================================================
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    
    axes[0].hist(seq_times, bins=20, color='blue', alpha=0.7, edgecolor='black')
    axes[0].set_title('Sequential\n(Baseline)', fontsize=12, fontweight='bold')
    axes[0].set_xlabel('Tiempo (s)')
    axes[0].set_ylabel('Frecuencia')
    axes[0].grid(True, alpha=0.3)
    
    axes[1].hist(fgmt_times, bins=20, color='green', alpha=0.7, edgecolor='black')
    axes[1].set_title('FGMT\n(Fine-Grained)', fontsize=12, fontweight='bold')
    axes[1].set_xlabel('Tiempo (s)')
    axes[1].set_ylabel('Frecuencia')
    axes[1].grid(True, alpha=0.3)
    
    axes[2].hist(cgmt_times, bins=20, color='red', alpha=0.7, edgecolor='black')
    axes[2].set_title('CGMT\n(Coarse-Grained)', fontsize=12, fontweight='bold')
    axes[2].set_xlabel('Tiempo (s)')
    axes[2].set_ylabel('Frecuencia')
    axes[2].grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(f"{GRAPHS_DIR}/01_histogram_comparativo.png", dpi=150)
    plt.close()
    
    # =====================================================================
    # 2. Boxplot comparativo
    # =====================================================================
    fig, ax = plt.subplots(figsize=(10, 6))
    
    data_to_plot = [seq_times, fgmt_times, cgmt_times]
    bp = ax.boxplot(data_to_plot, labels=['Sequential', 'FGMT', 'CGMT'],
                    patch_artist=True, widths=0.6)
    
    colors = ['blue', 'green', 'red']
    for patch, color in zip(bp['boxes'], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.7)
    
    ax.set_ylabel('Tiempo (s)', fontsize=12, fontweight='bold')
    ax.set_title('Comparativa de Tiempos de Ejecución\n(200 mediciones)', 
                 fontsize=14, fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')
    
    plt.tight_layout()
    plt.savefig(f"{GRAPHS_DIR}/02_boxplot_comparativo.png", dpi=150)
    plt.close()
    
    # =====================================================================
    # 3. Speed Up (vs Sequential)
    # =====================================================================
    seq_mean = seq_times.mean()
    fgmt_mean = fgmt_times.mean()
    cgmt_mean = cgmt_times.mean()
    
    speedup_fgmt = seq_mean / fgmt_mean
    speedup_cgmt = seq_mean / cgmt_mean
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    models = ['FGMT', 'CGMT']
    speedups = [speedup_fgmt, speedup_cgmt]
    colors_bar = ['green', 'red']
    
    bars = ax.bar(models, speedups, color=colors_bar, alpha=0.7, edgecolor='black', width=0.5)
    
    for i, (bar, speedup) in enumerate(zip(bars, speedups)):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{speedup:.2f}x',
                ha='center', va='bottom', fontsize=12, fontweight='bold')
    
    ax.axhline(y=1.0, color='blue', linestyle='--', linewidth=2, label='Sequential (baseline)')
    ax.set_ylabel('Speed Up (vs Sequential)', fontsize=12, fontweight='bold')
    ax.set_title('Speed Up Comparativo\n(Mayor es mejor)', fontsize=14, fontweight='bold')
    ax.set_ylim(0, max(speedups) * 1.2)
    ax.grid(True, alpha=0.3, axis='y')
    ax.legend()
    
    plt.tight_layout()
    plt.savefig(f"{GRAPHS_DIR}/03_speedup_comparison.png", dpi=150)
    plt.close()
    
    # =====================================================================
    # 4. Línea temporal de ejecuciones
    # =====================================================================
    fig, ax = plt.subplots(figsize=(12, 6))
    
    runs = np.arange(1, len(seq_times) + 1)
    
    ax.plot(runs, seq_times, 'b-', alpha=0.6, label='Sequential', linewidth=1)
    ax.plot(runs, fgmt_times, 'g-', alpha=0.6, label='FGMT', linewidth=1)
    ax.plot(runs, cgmt_times, 'r-', alpha=0.6, label='CGMT', linewidth=1)
    
    ax.axhline(y=seq_mean, color='blue', linestyle='--', linewidth=2, alpha=0.7)
    ax.axhline(y=fgmt_mean, color='green', linestyle='--', linewidth=2, alpha=0.7)
    ax.axhline(y=cgmt_mean, color='red', linestyle='--', linewidth=2, alpha=0.7)
    
    ax.set_xlabel('Ejecución #', fontsize=12, fontweight='bold')
    ax.set_ylabel('Tiempo (s)', fontsize=12, fontweight='bold')
    ax.set_title('Tiempos de Ejecución a lo Largo de 200 Runs', fontsize=14, fontweight='bold')
    ax.legend(loc='best')
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(f"{GRAPHS_DIR}/04_timeline_executions.png", dpi=150)
    plt.close()
    
    # =====================================================================
    # 5. Estadísticas de resumen
    # =====================================================================
    print("\n" + "="*70)
    print("ESTADÍSTICAS DE RESUMEN")
    print("="*70)
    
    print(f"\n{'Métrica':<20} {'Sequential':<15} {'FGMT':<15} {'CGMT':<15}")
    print("-" * 70)
    
    print(f"{'Promedio (s)':<20} {seq_mean:<15.6f} {fgmt_mean:<15.6f} {cgmt_mean:<15.6f}")
    print(f"{'Mínimo (s)':<20} {seq_times.min():<15.6f} {fgmt_times.min():<15.6f} {cgmt_times.min():<15.6f}")
    print(f"{'Máximo (s)':<20} {seq_times.max():<15.6f} {fgmt_times.max():<15.6f} {cgmt_times.max():<15.6f}")
    print(f"{'Desv. Est. (s)':<20} {seq_times.std():<15.6f} {fgmt_times.std():<15.6f} {cgmt_times.std():<15.6f}")
    print(f"{'Speed Up (vs Seq)':<20} {'1.00x':<15} {speedup_fgmt:<15.2f}x {speedup_cgmt:<15.2f}x")
    
    print("\n" + "="*70 + "\n")
    
except FileNotFoundError as e:
    print(f"Error: No se encontraron archivos CSV. {e}")
    exit(1)
except Exception as e:
    print(f"Error al generar gráficas: {e}")
    exit(1)

PYTHON_SCRIPT
    
    echo "    ✓ Gráficas generadas exitosamente"
else
    echo "    ⚠ Python3 no disponible. Omitiendo gráficas."
fi

# Resumen final
echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  ✓ MEDICIONES COMPLETADAS                                     ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "Resultados guardados en: $RESULTS_DIR"
echo "  ├─ CSV Sequential:   mediciones_secuencial.csv"
echo "  ├─ CSV FGMT:         mediciones_fgmt.csv"
echo "  ├─ CSV CGMT:         mediciones_cgmt.csv"
echo "  ├─ Gráficas:         graficas/"
echo "  │  ├─ 01_histogram_comparativo.png"
echo "  │  ├─ 02_boxplot_comparativo.png"
echo "  │  ├─ 03_speedup_comparison.png"
echo "  │  └─ 04_timeline_executions.png"
echo "  └─ Imágenes:         image/"
echo "     ├─ frame_secuencial.ppm"
echo "     ├─ frame_fgmt.ppm"
echo "     └─ frame_cgmt.ppm"
echo ""
echo "Para ver las gráficas:"
echo "  open $GRAPHS_DIR/*.png  (macOS)"
echo "  xdg-open $GRAPHS_DIR/*.png  (Linux)"
echo ""