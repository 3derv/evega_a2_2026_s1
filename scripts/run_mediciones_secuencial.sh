#!/bin/bash

# Script para ejecutar mediciones secuenciales 200 veces
# Uso: ./run_mediciones_secuencial.sh

NUM_RUNS=200
OUTPUT_DIR="/home/ederv/tec/p1arqui2/evega_a2_2026_s1/results"
CSV_FILE="$OUTPUT_DIR/mediciones_secuencial.csv"

echo "Ejecutando $NUM_RUNS mediciones secuenciales..."
cd /home/ederv/tec/p1arqui2/evega_a2_2026_s1/build
./raytracer --runs $NUM_RUNS

echo "Generando gráficas..."
cd /home/ederv/tec/p1arqui2/evega_a2_2026_s1
python3 scripts/generar_graficas.py $CSV_FILE

echo "Mediciones completadas. Revisa $OUTPUT_DIR para resultados y gráficas."