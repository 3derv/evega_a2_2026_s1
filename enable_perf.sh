#!/bin/bash
#
# enable_perf.sh — Habilitar perf con permisos adecuados
# Requiere: sudo con acceso
#
# Uso:
#   ./enable_perf.sh enable    # baja paranoid a 0
#   ./enable_perf.sh disable   # restaura paranoid a 4
#   ./enable_perf.sh status    # muestra estado actual
#

PARANOID_FILE="/proc/sys/kernel/perf_event_paranoid"

case "${1:-status}" in
  enable)
    echo "[*] Habilitando perf_events (paranoid=0)..."
    sudo bash -c "echo 0 > $PARANOID_FILE"
    echo "[✓] Nuevo valor:"
    cat "$PARANOID_FILE"
    echo ""
    echo "Ahora puedes correr perf sin restricciones de paranoid."
    echo "Ej: ./scripts/run_all_models.sh --with-perf"
    ;;
  disable)
    echo "[*] Restaurando perf_event_paranoid a 4..."
    sudo bash -c "echo 4 > $PARANOID_FILE"
    echo "[✓] Valor restaurado:"
    cat "$PARANOID_FILE"
    ;;
  status)
    echo "[i] Estado actual de perf_event_paranoid:"
    cat "$PARANOID_FILE"
    current=$(cat "$PARANOID_FILE")
    case "$current" in
      -1) echo "    → Modo más permisivo (todos los eventos)" ;;
      0) echo "    → Permisivo (sin raw/ftrace, con CAP_PERFMON)" ;;
      1) echo "    → Moderado (solo per-process, sin system-wide)" ;;
      2) echo "    → Restringido (solo user-space)" ;;
      4) echo "    → MUY RESTRINGIDO (requiere CAP_PERFMON)" ;;
      *) echo "    → Valor desconocido: $current" ;;
    esac
    ;;
  *)
    echo "Uso: $0 [enable|disable|status]"
    exit 1
    ;;
esac
