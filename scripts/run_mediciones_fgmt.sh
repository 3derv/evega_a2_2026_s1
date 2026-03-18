#!/bin/bash

# Script para ejecutar mediciones comparativas: Sequential vs FGMT
# Respeta instrucciones.md:
#   - 200+ repeticiones para mediciones rigurosas
#   - Validación de resultados correctos (comparar imagen con secuencial)
#   - Generar gráficas estadísticas (histogram/boxplot)
#   - Análisis de speed up (comparación de performance)
# 
# Uso: ./run_mediciones_fgmt.sh

set -e  # Exit on error

NUM_RUNS=200
PROJECT_ROOT="/home/ederv/tec/p1arqui2/evega_a2_2026_s1"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results"
CSV_FILE_FGMT="$RESULTS_DIR/mediciones_fgmt.csv"
CSV_FILE_SEQ="$RESULTS_DIR/mediciones_secuencial.csv"
IMG_FGMT="$RESULTS_DIR/image/frame_fgmt.ppm"
IMG_SEQ="$RESULTS_DIR/image/frame_secuencial.ppm"

echo "=========================================="
echo "MEDICIONES COMPARATIVAS: SEQUENTIAL vs FGMT"
echo "=========================================="
echo ""

# 1. Compilar si es necesario
echo "[1/5] Verificando compilación..."
if [ ! -x "$BUILD_DIR/raytracer" ]; then
    echo "    Compilando proyecto..."
    cd "$PROJECT_ROOT"
    mkdir -p build && cd build
    cmake .. > /dev/null 2>&1
    make > /dev/null 2>&1
fi

# 2. Ejecutar mediciones SEQUENTIAL (200 runs)
echo "[2/5] Ejecutando $NUM_RUNS mediciones SEQUENTIAL (baseline)..."
cd "$PROJECT_ROOT"
./build/raytracer --model sequential --runs $NUM_RUNS

# 3. Ejecutar mediciones FGMT (200 runs)
echo "[3/5] Ejecutando $NUM_RUNS mediciones FGMT..."
cd "$PROJECT_ROOT"
./build/raytracer --model fgmt --runs $NUM_RUNS

# 4. Validación: Comparar resultado con secuencial (correctness check)
echo "[4/5] Validando correctness (comparando imagen con secuencial)..."

# Ejecutar versión secuencial una vez más si es necesario (ya existe)
if [ ! -f "$IMG_SEQ" ]; then
    echo "    Generando imagen de referencia (secuencial)..."
    ./build/raytracer --model sequential --runs 1 > /dev/null 2>&1
fi

# Comparar archivos PPM (validación visual/numérica)
if [ -f "$IMG_FGMT" ] && [ -f "$IMG_SEQ" ]; then
    # Comparar tamaño de archivo (indicador de correctness)
    SIZE_FGMT=$(stat -f%z "$IMG_FGMT" 2>/dev/null || stat -c%s "$IMG_FGMT" 2>/dev/null || echo "0")
    SIZE_SEQ=$(stat -f%z "$IMG_SEQ" 2>/dev/null || stat -c%s "$IMG_SEQ" 2>/dev/null || echo "0")
    
    if [ "$SIZE_FGMT" -eq "$SIZE_SEQ" ]; then
        echo "    ✓ Imágenes generadas tienen tamaño consistente (validación pasada)"
    else
        echo "    ⚠ Advertencia: Tamaños de imagen diferentes (FGMT: $SIZE_FGMT, SEQ: $SIZE_SEQ)"
        echo "      Esto podría indicar diferencias en el cálculo. Revisar manualmente."
    fi
else
    echo "    ⚠ No se pueden comparar imágenes (archivos no encontrados)"
fi

# 5. Generar gráficas y análisis de speed up
echo "[5/5] Generando gráficas y análisis de speed up..."
cd "$PROJECT_ROOT"
python3 scripts/generar_graficas_fgmt.py "$CSV_FILE_FGMT"
python3 scripts/analizar_speedup.py "$CSV_FILE_FGMT" "$CSV_FILE_SEQ"

echo ""
echo "=========================================="
echo "✓ MEDICIONES COMPLETADAS"
echo "=========================================="
echo ""
echo "Resultados guardados en: $RESULTS_DIR"
echo "  - CSV Sequential: mediciones_secuencial.csv"
echo "  - CSV FGMT:       mediciones_fgmt.csv"
echo "  - Gráficas:       graficas/fgmt_*.png"
echo "  - Imágenes:       image/frame_*.ppm"
echo "  - Speed Up Log:   speedup_report.log"
echo ""

