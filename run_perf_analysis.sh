#!/bin/bash
#
# run_perf_analysis.sh — Flujo completo con perf habilitado
#
# Pasos:
#   1. Pide contraseña de sudo (se mantiene en sesión)
#   2. Baja perf_event_paranoid a 0
#   3. Ejecuta ./scripts/run_all_models.sh --with-perf
#   4. Restaura perf_event_paranoid a 4
#   5. Genera reporte final
#

set -e

PARANOID_FILE="/proc/sys/kernel/perf_event_paranoid"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  ANÁLISIS COMPLETO CON PERF + CONTEXTO_SWITCH_COST EN CGMT  ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# ──────────────────────────────────────────────────────────────────
# 1. Validar acceso a sudo
# ──────────────────────────────────────────────────────────────────

echo "[1/5] Validando acceso a sudo..."
if ! sudo -n true 2>/dev/null; then
  echo "  → Se requiere contraseña de sudo (ingresada una sola vez)"
  sudo -v  # Pide contraseña y mantiene sesión
  echo "  ✓ Acceso sudo confirmado"
else
  echo "  ✓ Acceso sudo disponible sin contraseña"
fi
echo ""

# ──────────────────────────────────────────────────────────────────
# 2. Bajar perf_event_paranoid a 0
# ──────────────────────────────────────────────────────────────────

echo "[2/5] Estado actual de perf_event_paranoid:"
ORIGINAL_PARANOID=$(cat "$PARANOID_FILE")
echo "  Valor: $ORIGINAL_PARANOID"
echo ""

echo "[2b/5] Bajando perf_event_paranoid a 0 (permite acceso a contadores)..."
sudo bash -c "echo 0 > $PARANOID_FILE"
echo "  ✓ Nuevo valor: $(cat $PARANOID_FILE)"
echo ""

# ──────────────────────────────────────────────────────────────────
# 3. Ejecutar flujo completo de mediciones
# ──────────────────────────────────────────────────────────────────

echo "[3/5] Ejecutando ./scripts/run_all_models.sh --with-perf"
echo "  Esto corre 200 frames × 5 modelos con perf stat..."
echo ""

./scripts/run_all_models.sh --with-perf

echo ""

# ──────────────────────────────────────────────────────────────────
# 4. Restaurar perf_event_paranoid
# ──────────────────────────────────────────────────────────────────

echo "[4/5] Restaurando perf_event_paranoid a $ORIGINAL_PARANOID..."
if [[ "$ORIGINAL_PARANOID" != "$(cat $PARANOID_FILE)" ]]; then
  sudo bash -c "echo $ORIGINAL_PARANOID > $PARANOID_FILE"
  echo "  ✓ Restaurado: $(cat $PARANOID_FILE)"
else
  echo "  (ya está en el valor original)"
fi
echo ""

# ──────────────────────────────────────────────────────────────────
# 5. Resumen final
# ──────────────────────────────────────────────────────────────────

echo "[5/5] ✓ ANÁLISIS COMPLETADO"
echo ""
echo "Archivos generados:"
echo "  Mediciones      : results/mediciones_*.csv"
echo "  Reporte speedup : results/speedup_report.log"
echo "  Gráficas        : results/graficas/*.png"
echo "  Imágenes PPM    : results/image/frame_*.ppm"
echo "  Perfilado perf  : results/perf/*"
echo ""
echo "Próximos pasos:"
echo "  1. Revisar results/speedup_report.log para VT comparativos"
echo "  2. Analizar results/perf/perf_results.json para contadores hardware"
echo "  3. Comparar CGMT: VT anterior vs nuevo (con CONTEXT_SWITCH_COST_NS)"
echo ""
