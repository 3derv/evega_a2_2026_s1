#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

if ! command -v python3 >/dev/null 2>&1; then
  echo "Error: python3 no esta instalado"
  exit 1
fi

PROFILE="${1:-plan-medium}"

# Requiere sudo cacheado para alternar SMT hardware on/off sin prompts intermedios
if ! sudo -n true 2>/dev/null; then
  echo "Se requiere sudo cacheado. Ejecuta primero: sudo -v"
  exit 1
fi

case "$PROFILE" in
  plan-quick)
    # Smoke test rapido para validar pipeline end-to-end
    SIZES="80x60"
    THREADS="2,4"
    MODELS="sequential,smt,cmp"
    ;;
  plan-medium)
    # Plan recomendado para informe: resoluciones moderadas + sweep de hilos
    SIZES="80x60,120x90,160x120"
    THREADS="2,4,8"
    MODELS="sequential,fgmt,cgmt,smt,cmp"
    ;;
  plan-deep)
    # Plan extenso (puede tomar varias horas dependiendo de FGMT)
    SIZES="80x60,120x90,160x120,240x180"
    THREADS="2,4,8,12"
    MODELS="sequential,fgmt,cgmt,smt,cmp"
    ;;
  *)
    echo "Uso: $0 [plan-quick|plan-medium|plan-deep]"
    exit 1
    ;;
esac

echo "[INFO] Perfil: $PROFILE"
echo "[INFO] Sizes: $SIZES"
echo "[INFO] Threads: $THREADS"
echo "[INFO] Models: $MODELS"

echo "[INFO] Ejecutando matriz de experimentos..."
python3 scripts/run_scalability_matrix.py \
  --project-root "$PROJECT_ROOT" \
  --sizes "$SIZES" \
  --threads "$THREADS" \
  --models "$MODELS" \
  --smt-states "on,off"

echo "[INFO] Experimentos completados"
