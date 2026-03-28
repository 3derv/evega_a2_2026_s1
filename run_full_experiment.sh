#!/usr/bin/env bash
# run_full_experiment.sh — Experimento completo para entrega final (rúbrica A2)
#
# Orquesta el flujo íntegro de medición:
#   1. Validar entorno (sudo, perf, ejecutable)
#   2. Capturar información del hardware → docs/hw_info.txt
#   3. Guardar y establecer CPU governor = performance
#   4. Guardar y bajar perf_event_paranoid a 0
#   5. Ejecutar los 5 modelos con perf stat (200 frames cada uno)
#   6. Ejecutar comparativa SMT ON vs OFF (modelo CMP)
#   7. Restaurar CPU governor original
#   8. Restaurar perf_event_paranoid original
#   9. Mostrar resumen de todos los artefactos generados
#
# Uso: ./run_full_experiment.sh [--skip-smt-comparison] [--skip-fgmt]
#
# --skip-smt-comparison  Omite run_smt_comparison.sh (no requiere hardware físico)
# --skip-fgmt            Pasa --skip-fgmt a run_all_models.sh (ahorra ~102s)
#
# NOTA: Requiere sudo para controlar perf_event_paranoid y CPU governor.
#       En WSL2 los contadores hardware perf pueden no estar disponibles.

set -euo pipefail

# ─────────────────────────────────────────────────────────────────────────────
# Constantes y flags
# ─────────────────────────────────────────────────────────────────────────────
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$PROJECT_ROOT/scripts"
RESULTS_DIR="$PROJECT_ROOT/results"
PERF_DIR="$RESULTS_DIR/perf"
DOCS_DIR="$PROJECT_ROOT/docs"
HW_INFO="$DOCS_DIR/hw_info.txt"
PARANOID_FILE="/proc/sys/kernel/perf_event_paranoid"
GOVERNOR_DIR="/sys/devices/system/cpu/cpu0/cpufreq"

SKIP_SMT_COMP=false
EXTRA_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --skip-smt-comparison) SKIP_SMT_COMP=true ;;
        --skip-fgmt)           EXTRA_ARGS+=("--skip-fgmt") ;;
        --skip-smt)            EXTRA_ARGS+=("--skip-smt") ;;
    esac
done

cd "$PROJECT_ROOT"

# Caché para restauración al salir (incluso por error)
ORIGINAL_PARANOID=""
ORIGINAL_GOVERNOR=""

restore_env() {
    local exit_code=$?
    echo ""
    echo "── Restaurando entorno ──────────────────────────────────────────────"
    if [[ -n "$ORIGINAL_PARANOID" && -f "$PARANOID_FILE" ]]; then
        local current
        current=$(cat "$PARANOID_FILE")
        if [[ "$current" != "$ORIGINAL_PARANOID" ]]; then
            sudo bash -c "echo $ORIGINAL_PARANOID > $PARANOID_FILE" 2>/dev/null || true
            echo "  ✓ perf_event_paranoid restaurado a $ORIGINAL_PARANOID"
        fi
    fi
    if [[ -n "$ORIGINAL_GOVERNOR" && -f "$GOVERNOR_DIR/scaling_governor" ]]; then
        local ncpus
        ncpus=$(nproc)
        for ((i = 0; i < ncpus; i++)); do
            local gfile="/sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor"
            if [[ -f "$gfile" ]]; then
                sudo bash -c "echo $ORIGINAL_GOVERNOR > $gfile" 2>/dev/null || true
            fi
        done
        echo "  ✓ CPU governor restaurado a $ORIGINAL_GOVERNOR"
    fi
    if [[ $exit_code -ne 0 ]]; then
        echo "  ✗ Script terminó con error (código $exit_code)"
    fi
}
trap restore_env EXIT

# ─────────────────────────────────────────────────────────────────────────────
# Cabecera
# ─────────────────────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════════════╗"
echo "║  EXPERIMENTO COMPLETO — FRAMEWORK MULTITHREADING (A2 · 1S2026)      ║"
echo "║  Sequential · FGMT · CGMT · SMT · CMP + perf + SMT ON/OFF           ║"
echo "╚══════════════════════════════════════════════════════════════════════╝"
echo ""
[[ "${#EXTRA_ARGS[@]}" -gt 0 ]] && echo "  ⚠ Flags adicionales a run_all_models.sh: ${EXTRA_ARGS[*]}"
[[ "$SKIP_SMT_COMP" = true ]] && echo "  ⚠ Comparativa SMT ON/OFF omitida (--skip-smt-comparison)"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 1 — Validar entorno
# ─────────────────────────────────────────────────────────────────────────────
echo "[1/9] Validando entorno..."

# Detectar WSL2 (los contadores hardware pueden no funcionar)
IS_WSL2=false
if grep -qi "microsoft\|wsl" /proc/version 2>/dev/null; then
    IS_WSL2=true
    echo "  ⚠ WSL2 detectado — contadores hardware perf pueden no estar disponibles"
    echo "    (VT virtual y speedup siguen siendo válidos)"
fi

# Verificar sudo
if ! sudo -n true 2>/dev/null; then
    echo "  → Se requiere contraseña de sudo (usada una sola vez en el flujo)"
    sudo -v
fi
echo "  ✓ Acceso sudo confirmado"

# Verificar ejecutable
if [ ! -f "$PROJECT_ROOT/build/raytracer" ]; then
    echo "  ℹ Compilando proyecto antes de continuar..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    cmake --build build/ -- -j4 > /dev/null 2>&1
fi
echo "  ✓ Ejecutable disponible: build/raytracer"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 2 — Capturar información del hardware
# ─────────────────────────────────────────────────────────────────────────────
echo "[2/9] Capturando información del hardware → $HW_INFO"

mkdir -p "$DOCS_DIR"
{
    echo "# hw_info.txt — Entorno de ejecución del experimento A2"
    echo "# Generado: $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""

    echo "## Sistema Operativo"
    uname -a
    echo ""

    echo "## Procesador (modelo)"
    grep "model name" /proc/cpuinfo | sort -u
    echo ""

    echo "## Núcleos lógicos disponibles"
    echo "  nproc: $(nproc)"
    echo ""

    echo "## Resumen lscpu"
    lscpu 2>/dev/null || echo "  lscpu no disponible"
    echo ""

    echo "## Frecuencia actual (MHz)"
    if [[ -f "$GOVERNOR_DIR/scaling_cur_freq" ]]; then
        awk '{printf "  %.0f MHz\n", $1/1000}' "$GOVERNOR_DIR/scaling_cur_freq"
    else
        grep "cpu MHz" /proc/cpuinfo | head -4 || echo "  No disponible"
    fi
    echo ""

    echo "## CPU Governor actual"
    if [[ -f "$GOVERNOR_DIR/scaling_governor" ]]; then
        cat "$GOVERNOR_DIR/scaling_governor"
    else
        echo "  scaling_governor no disponible (WSL2 o VM)"
    fi
    echo ""

    echo "## SMT / Hyper-Threading"
    if [[ -f "/sys/devices/system/cpu/smt/control" ]]; then
        echo "  Estado: $(cat /sys/devices/system/cpu/smt/control)"
    else
        echo "  /sys/devices/system/cpu/smt/control no disponible (WSL2 o VM)"
    fi
    echo ""

    echo "## Memoria RAM"
    free -h 2>/dev/null || echo "  free no disponible"
    echo ""

    echo "## perf_event_paranoid"
    if [[ -f "$PARANOID_FILE" ]]; then
        echo "  Valor antes del experimento: $(cat $PARANOID_FILE)"
    else
        echo "  $PARANOID_FILE no existe"
    fi
} > "$HW_INFO"

echo "  ✓ hw_info escrito en $HW_INFO"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 3 — Establecer CPU governor = performance
# ─────────────────────────────────────────────────────────────────────────────
echo "[3/9] Configurando CPU governor..."

if [[ -f "$GOVERNOR_DIR/scaling_governor" ]]; then
    ORIGINAL_GOVERNOR=$(cat "$GOVERNOR_DIR/scaling_governor")
    echo "  Governor actual: $ORIGINAL_GOVERNOR"
    if [[ "$ORIGINAL_GOVERNOR" != "performance" ]]; then
        NCPUS=$(nproc)
        for ((i = 0; i < NCPUS; i++)); do
            GFILE="/sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor"
            if [[ -f "$GFILE" ]]; then
                sudo bash -c "echo performance > $GFILE" 2>/dev/null || true
            fi
        done
        NEW_GOV=$(cat "$GOVERNOR_DIR/scaling_governor")
        echo "  ✓ Governor establecido a: $NEW_GOV"
    else
        echo "  ✓ Governor ya es 'performance', sin cambios"
    fi
else
    echo "  ℹ scaling_governor no disponible (WSL2/VM) — se continúa sin cambio"
fi
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 4 — Bajar perf_event_paranoid a 0
# ─────────────────────────────────────────────────────────────────────────────
echo "[4/9] Configurando perf_event_paranoid..."

if [[ -f "$PARANOID_FILE" ]]; then
    ORIGINAL_PARANOID=$(cat "$PARANOID_FILE")
    echo "  Valor actual: $ORIGINAL_PARANOID"
    if [[ "$ORIGINAL_PARANOID" != "0" ]]; then
        sudo bash -c "echo 0 > $PARANOID_FILE"
        echo "  ✓ perf_event_paranoid establecido a 0 (habilita contadores hardware)"
    else
        echo "  ✓ Ya está en 0, sin cambios"
    fi
else
    echo "  ℹ $PARANOID_FILE no existe — perf hardware puede no funcionar"
fi
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 5 — Ejecutar los 5 modelos con perf stat
# ─────────────────────────────────────────────────────────────────────────────
echo "[5/9] Ejecutando los 5 modelos con medición de VT y perf stat..."
echo ""

"$SCRIPTS_DIR/run_all_models.sh" --with-perf "${EXTRA_ARGS[@]}"

echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 6 — Comparativa SMT ON vs OFF
# ─────────────────────────────────────────────────────────────────────────────
echo ""
if [[ "$SKIP_SMT_COMP" = false ]]; then
    echo "[6/9] Ejecutando comparativa SMT (Hyper-Threading) ON vs OFF..."
    echo ""
    "$SCRIPTS_DIR/run_smt_comparison.sh" || {
        echo "  ⚠ run_smt_comparison.sh terminó con error (puede ser WSL2/VM — continúa)"
    }
else
    echo "[6/9] ⚠ Comparativa SMT ON/OFF omitida por flag --skip-smt-comparison"
fi
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 7 — Restaurar CPU governor
# ─────────────────────────────────────────────────────────────────────────────
echo "[7/9] Restaurando CPU governor..."
# La restauración real ocurre en el trap EXIT. Aquí solo se reporta el estado.
if [[ -n "$ORIGINAL_GOVERNOR" && "$ORIGINAL_GOVERNOR" != "performance" ]]; then
    NCPUS=$(nproc)
    for ((i = 0; i < NCPUS; i++)); do
        GFILE="/sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor"
        if [[ -f "$GFILE" ]]; then
            sudo bash -c "echo $ORIGINAL_GOVERNOR > $GFILE" 2>/dev/null || true
        fi
    done
    CURRENT_GOV=""
    [[ -f "$GOVERNOR_DIR/scaling_governor" ]] && CURRENT_GOV=$(cat "$GOVERNOR_DIR/scaling_governor")
    echo "  ✓ CPU governor restaurado a $ORIGINAL_GOVERNOR (actual: ${CURRENT_GOV:-n/a})"
    ORIGINAL_GOVERNOR=""  # Evitar doble restauración en el trap
else
    echo "  ✓ Sin cambio de governor a restaurar"
fi
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 8 — Restaurar perf_event_paranoid
# ─────────────────────────────────────────────────────────────────────────────
echo "[8/9] Restaurando perf_event_paranoid..."
if [[ -n "$ORIGINAL_PARANOID" && -f "$PARANOID_FILE" ]]; then
    CURRENT=$(cat "$PARANOID_FILE")
    if [[ "$CURRENT" != "$ORIGINAL_PARANOID" ]]; then
        sudo bash -c "echo $ORIGINAL_PARANOID > $PARANOID_FILE"
        echo "  ✓ Restaurado a $ORIGINAL_PARANOID (era 0 durante el experimento)"
    else
        echo "  ✓ Ya está en el valor original ($ORIGINAL_PARANOID)"
    fi
    ORIGINAL_PARANOID=""  # Evitar doble restauración en el trap
else
    echo "  ✓ Sin cambio de paranoid a restaurar"
fi
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Paso 9 — Resumen completo de artefactos
# ─────────────────────────────────────────────────────────────────────────────
echo "[9/9] ╔══════════════════════════════════════════════════════════════╗"
echo "       ║  EXPERIMENTO COMPLETADO — ARTEFACTOS GENERADOS              ║"
echo "       ╚══════════════════════════════════════════════════════════════╝"
echo ""

# ── Mediciones CSV ────────────────────────────────────────────────────────
echo "  ► Mediciones de VT (reloj virtual, IC95):"
for csv in "$RESULTS_DIR"/mediciones_*.csv; do
    [[ -f "$csv" ]] || continue
    frames=$(( $(wc -l < "$csv") - 1 ))
    printf "    %-45s  %d frames\n" "${csv#$PROJECT_ROOT/}" "$frames"
done
echo ""

# ── Imágenes PPM ──────────────────────────────────────────────────────────
echo "  ► Imágenes de correctness (byte-exact):"
for ppm in "$RESULTS_DIR/image"/frame_*.ppm; do
    [[ -f "$ppm" ]] || continue
    size=$(wc -c < "$ppm")
    printf "    %-45s  %d bytes\n" "${ppm#$PROJECT_ROOT/}" "$size"
done
echo ""

# ── Gráficas PNG ─────────────────────────────────────────────────────────
echo "  ► Gráficas de análisis:"
for png in "$RESULTS_DIR/graficas"/*.png; do
    [[ -f "$png" ]] || continue
    printf "    %s\n" "${png#$PROJECT_ROOT/}"
done
echo ""

# ── Perf ─────────────────────────────────────────────────────────────────
echo "  ► Perfilado hardware (perf stat):"
PERF_COUNT=0
for txt in "$PERF_DIR"/perf_*.txt; do
    [[ -f "$txt" ]] && PERF_COUNT=$(( PERF_COUNT + 1 ))
done
if [[ $PERF_COUNT -gt 0 ]]; then
    for txt in "$PERF_DIR"/perf_*.txt; do
        [[ -f "$txt" ]] || continue
        printf "    %s\n" "${txt#$PROJECT_ROOT/}"
    done
    [[ -f "$PERF_DIR/perf_results.json" ]] && \
        printf "    %s\n" "results/perf/perf_results.json  (CPI, IPC, cache miss %)"
else
    echo "    (sin archivos perf — posible entorno WSL2/VM)"
fi
echo ""

# ── hw_info ───────────────────────────────────────────────────────────────
echo "  ► Info del hardware:"
printf "    %s\n" "${HW_INFO#$PROJECT_ROOT/}"
echo ""

# ── Speedup log ───────────────────────────────────────────────────────────
[[ -f "$RESULTS_DIR/speedup_report.log" ]] && \
    echo "  ► Reporte speedup : results/speedup_report.log"

# ── SMT CSV ───────────────────────────────────────────────────────────────
SMT_ON="$RESULTS_DIR/mediciones_cmp_smt_on.csv"
SMT_OFF="$RESULTS_DIR/mediciones_cmp_smt_off.csv"
if [[ -f "$SMT_ON" || -f "$SMT_OFF" ]]; then
    echo "  ► Comparativa SMT:"
    [[ -f "$SMT_ON"  ]] && printf "    results/mediciones_cmp_smt_on.csv\n"
    [[ -f "$SMT_OFF" ]] && printf "    results/mediciones_cmp_smt_off.csv\n"
fi
echo ""

# ── Mapa rúbrica ─────────────────────────────────────────────────────────
echo "  ═══════════════════════════════════════════════════════════════"
echo "  MAPA RÚBRICA → ARTEFACTO"
echo "  ═══════════════════════════════════════════════════════════════"
echo "  Cat 1 (Implementación modelos)  → results/image/frame_*.ppm"
echo "  Cat 2 (VT y métricas)           → results/mediciones_*.csv + graficas/"
echo "  Cat 3 (Contadores hardware)     → results/perf/perf_*.txt (.json)"
echo "  Cat 4 (Comparativa SMT ON/OFF)  → results/mediciones_cmp_smt_*.csv"
echo "  Cat 5 (Análisis IC95 + speedup) → results/graficas/*.png + speedup_report.log"
echo "  ═══════════════════════════════════════════════════════════════"
echo ""
echo "  ✓ Todo listo. Consultar docs/hw_info.txt para contexto del entorno."
echo ""
