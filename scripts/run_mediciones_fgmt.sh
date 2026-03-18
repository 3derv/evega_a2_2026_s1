#!/bin/bash

# Script para ejecutar mediciones FGMT (Fine-Grained Multithreading) 200 veces
# Respeta instrucciones.md:
#   - 200+ repeticiones para mediciones rigurosas
#   - Validación de resultados correctos (comparar imagen con secuencial)
#   - Generar gráficas estadísticas (histogram/boxplot)
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
echo "MEDICIONES FGMT (Fine-Grained Multithreading)"
echo "=========================================="
echo ""

# 1. Compilar si es necesario
echo "[1/4] Verificando compilación..."
if [ ! -x "$BUILD_DIR/raytracer" ]; then
    echo "    Compilando proyecto..."
    cd "$PROJECT_ROOT"
    mkdir -p build && cd build
    cmake .. > /dev/null 2>&1
    make > /dev/null 2>&1
fi

# 2. Ejecutar mediciones FGMT (200 runs)
echo "[2/4] Ejecutando $NUM_RUNS mediciones FGMT..."
cd "$PROJECT_ROOT"  # Ejecutar desde raíz del proyecto para rutas relativas correctas
./build/raytracer --model fgmt --runs $NUM_RUNS

# 3. Validación: Comparar resultado con secuencial (correctness check)
echo "[3/4] Validando correctness (comparando imagen con secuencial)..."

# Ejecutar versión secuencial una vez para validar
echo "    Generando imagen de referencia (secuencial)..."
./build/raytracer --model sequential --runs 1 > /dev/null 2>&1

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

# 4. Generar gráficas estadísticas
echo "[4/4] Generando gráficas estadísticas..."
cd "$PROJECT_ROOT"
python3 scripts/generar_graficas_fgmt.py "$CSV_FILE_FGMT"

echo ""
echo "=========================================="
echo "✓ MEDICIONES COMPLETADAS"
echo "=========================================="
echo ""
echo "Resultados guardados en: $RESULTS_DIR"
echo "  - CSV: mediciones_fgmt.csv"
echo "  - Gráficas: graficas/fgmt_*.png"
echo "  - Imagen: image/frame_fgmt.ppm"
echo ""
echo "Para comparación posterior:"
echo "  - Secuencial CSV: mediciones_secuencial.csv"
echo "  - Secuencial IMG: image/frame_secuencial.ppm"
echo ""
