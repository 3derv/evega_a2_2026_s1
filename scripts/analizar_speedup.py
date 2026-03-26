#!/usr/bin/env python3
"""
Analizar speed up comparando Sequential vs FGMT vs CGMT.
Lee los tres CSVs de mediciones, calcula estadísticas, genera log y gráficas.

Uso (invocado desde run_all_models.sh):
  python3 analizar_speedup.py <csv_seq> <csv_fgmt> <csv_cgmt> \
      --graphs <dir> --log <log_file>

Uso legacy (un solo modelo contra sequential):
  python3 analizar_speedup.py <csv_model> [csv_sequential]
"""

import sys
import os
import argparse
from pathlib import Path
import numpy as np
from datetime import datetime

# Clasificación de modelos por tipo de métrica:
#   SCHED_MODELS: modelan un CLK virtual → usar Tiempo Virtual como métrica de speedup.
#     El CPU time en estos modelos es sobrecarga de emulación, NO rendimiento del scheduler.
#   PERF_MODELS:  paralelismo real → usar CPU wall-clock como métrica de rendimiento.
SCHED_MODELS = {'sequential', 'fgmt', 'cgmt'}
PERF_MODELS  = {'smt', 'cmp'}

# ─── helpers ──────────────────────────────────────────────────────────────────

def analyze_csv(csv_path):
    """Leer CSV y calcular estadísticas de tiempo y tiempo virtual."""
    if not os.path.exists(csv_path):
        return None

    rows = []
    vt_rows = []
    stall_rows = []
    with open(csv_path) as f:
        header = f.readline().strip().split(',')
        try:
            t_idx = header.index('Tiempo(s)')
        except ValueError:
            for alt in ['time_ms', 'time_seconds']:
                if alt in header:
                    t_idx = header.index(alt)
                    break
            else:
                return None
        vt_idx    = header.index('TiempoVirtual(ns)') if 'TiempoVirtual(ns)' in header else None
        stall_idx = header.index('Stalls')            if 'Stalls'            in header else None

        for line in f:
            parts = line.strip().split(',')
            if len(parts) <= t_idx:
                continue
            try:
                t = float(parts[t_idx])
                if 'ms' in header[t_idx].lower():
                    t /= 1000
                rows.append(t)
                if vt_idx is not None and len(parts) > vt_idx:
                    vt_rows.append(int(parts[vt_idx]))
                if stall_idx is not None and len(parts) > stall_idx:
                    stall_rows.append(int(parts[stall_idx]))
            except ValueError:
                continue

    if not rows:
        return None

    times = np.array(rows)
    result = {
        'count': len(times),
        'mean':   times.mean(),
        'median': float(np.median(times)),
        'std':    times.std(),
        'min':    times.min(),
        'max':    times.max(),
        'times':  times,
        'vt_mean':    int(np.mean(vt_rows))    if vt_rows    else None,
        'vt_times':   np.array(vt_rows)         if vt_rows    else None,
        'stall_counts': stall_rows              if stall_rows else None,
    }
    return result


# Mapa de nombre de modelo → nombre del archivo PPM
PPM_FILENAME = {
    'sequential': 'frame_secuencial.ppm',
    'fgmt':       'frame_fgmt.ppm',
    'cgmt':       'frame_cgmt.ppm',
    'smt':        'frame_smt.ppm',
    'cmp':        'frame_cmp.ppm',
}


def read_ppm_pixels(path):
    """Lee el encabezado de un PPM (P3 o P6) y devuelve (width, height, total).
    Devuelve None si el archivo no existe o no es un PPM válido.
    """
    if not os.path.exists(path):
        return None
    try:
        tokens = []
        with open(path, 'rb') as f:
            while len(tokens) < 4:          # magic + w + h + maxval
                raw = f.readline()
                if not raw:
                    break
                line = raw.decode('ascii', errors='ignore').strip()
                if line.startswith('#') or not line:
                    continue
                tokens.extend(line.split())
        if len(tokens) < 3:
            return None
        magic = tokens[0]
        if magic not in ('P3', 'P6'):
            return None
        w, h = int(tokens[1]), int(tokens[2])
        return (w, h, w * h)
    except Exception:
        return None


def speedup_status(ratio):
    if ratio > 1.0:
        return f"FASTER  (+{(ratio-1)*100:.1f}%)"
    elif ratio < 1.0:
        return f"SLOWER  (-{(1-ratio)*100:.1f}%)"
    return "EQUAL"


def format_stats(label, s):
    if s is None:
        return f"{label}: archivo no encontrado\n"
    model_key = label.lower().strip()
    is_perf   = any(m in model_key for m in PERF_MODELS)
    cpu_label = "Tiempo CPU (throughput real)" if is_perf else "Tiempo CPU (overhead emulación — no es métrica de rendimiento)"
    has_vt = s['vt_mean'] is not None and s['vt_times'] is not None
    lines = [f"{label}:"]
    lines.append(f"  Mediciones : {s['count']}")
    if has_vt:
        vt = s['vt_times'] / 1e6  # ns → ms
        lines.append(f"  --- Tiempo Virtual (reloj simulado) ---")
        lines.append(f"  Promedio   : {s['vt_mean']/1e6:.3f} ms")
        lines.append(f"  Mediana    : {float(np.median(vt)):.3f} ms")
        lines.append(f"  Mínimo     : {vt.min():.3f} ms")
        lines.append(f"  Máximo     : {vt.max():.3f} ms")
        lines.append(f"  Desv. Est. : {vt.std():.3f} ms")
    lines.append(f"  --- {cpu_label} ---")
    lines.append(f"  Promedio   : {s['mean']:.6f} s")
    lines.append(f"  Mediana    : {s['median']:.6f} s")
    lines.append(f"  Mínimo     : {s['min']:.6f} s")
    lines.append(f"  Máximo     : {s['max']:.6f} s")
    lines.append(f"  Desv. Est. : {s['std']:.6f} s")
    if s.get('avg_stalls') is not None:
        lines.append(f"  --- Arquitectura ---")
        lines.append(f"  Stalls Prom.: {s['avg_stalls']:.1f}")
    if s.get('cpi') is not None:
        lines.append(f"  CPI          : {s['cpi']:.4f}")
    return "\n".join(lines)


# ─── gráficas ─────────────────────────────────────────────────────────────────

def generate_graphs(stats, graphs_dir):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("    ⚠ matplotlib no disponible. Omitiendo gráficas.")
        return

    os.makedirs(graphs_dir, exist_ok=True)
    labels = list(stats.keys())
    # Color explícito para cada modelo — consistente en TODAS las gráficas
    colors = {
        'sequential': 'royalblue',
        'fgmt':       'seagreen',
        'cgmt':       'tomato',
        'smt':        'mediumpurple',
        'cmp':        'darkorange',
    }
    clr = [colors.get(l, 'slategray') for l in labels]

    # Usar tiempo virtual (ns→ms) cuando esté disponible; caer a CPU si no
    def vt_series(s):
        if s['vt_times'] is not None:
            return s['vt_times'] / 1e6   # ns → ms
        return s['times'] * 1e3          # s → ms (fallback)

    def vt_mean_ms(s):
        if s['vt_mean'] is not None:
            return s['vt_mean'] / 1e6
        return s['mean'] * 1e3

    vt_all  = [vt_series(stats[l]) for l in labels]
    vt_avgs = [vt_mean_ms(stats[l]) for l in labels]

    # Rango global de VT para compartir eje X en histogramas
    vt_global_min = min(v.min() for v in vt_all)
    vt_global_max = max(v.max() for v in vt_all)
    vt_margin     = max((vt_global_max - vt_global_min) * 0.1, 0.001)

    # ── 1. Histograma de tiempo virtual por frame — eje X compartido ─────────
    # Eje X idéntico en todos los subplots para comparación visual directa.
    fig, axes = plt.subplots(1, len(labels), figsize=(6 * len(labels), 6),
                             sharey=False)
    if len(labels) == 1:
        axes = [axes]
    for ax, lbl, vt, c in zip(axes, labels, vt_all, clr):
        # Si la varianza es casi cero (FGMT, SMT), usar 1 bin para evitar
        # artefactos; de lo contrario 25 bins.
        n_bins = 1 if vt.std() < 1e-6 else 25
        ax.hist(vt, bins=n_bins, color=c, alpha=0.75, edgecolor='black')
        ax.axvline(vt.mean(), color='black', linestyle='--', linewidth=1.5,
                   label=f'μ={vt.mean():.3f} ms\nσ={vt.std():.4f} ms')
        ax.set_xlim(vt_global_min - vt_margin, vt_global_max + vt_margin)
        ax.set_title(lbl.upper(), fontweight='bold', fontsize=13)
        ax.set_xlabel('Tiempo Virtual por frame (ms)', fontsize=10)
        ax.set_ylabel('Frecuencia (frames)', fontsize=10)
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)
    plt.suptitle('Distribución del Tiempo Virtual por Frame — por Modelo\n'
                 '(eje X compartido para comparación directa)',
                 fontweight='bold', fontsize=13)
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/01_histogram_comparativo.png", dpi=300, bbox_inches='tight')
    plt.close()

    # ── 2. Boxplot de tiempo virtual — eje Y acotado ──────────────────────────
    # Y empieza cerca del mínimo para amplificar diferencias entre modelos.
    fig, ax = plt.subplots(figsize=(10, 7))
    bp = ax.boxplot(vt_all, labels=[l.upper() for l in labels],
                    patch_artist=True, widths=0.5)
    for patch, c in zip(bp['boxes'], clr):
        patch.set_facecolor(c)
        patch.set_alpha(0.7)
    # Anotar mediana de cada caja
    for i, (med_line, lbl) in enumerate(zip(bp['medians'], labels), start=1):
        med_val = med_line.get_ydata()[0]
        ax.text(i, med_val, f'  {med_val:.3f}', va='center',
                fontsize=9, fontweight='bold', color='black')
    y_lo = vt_global_min - vt_margin * 3
    y_hi = vt_global_max + vt_margin * 3
    ax.set_ylim(y_lo, y_hi)
    ax.set_ylabel('Tiempo Virtual por frame (ms)', fontweight='bold')
    ax.set_title('Distribución del Tiempo Virtual por Frame\n'
                 '(eje Y acotado — mismo quantum y costo de stall para todos)',
                 fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')
    ax.annotate('⚠ Eje Y no empieza en 0', xy=(0.01, 0.02),
                xycoords='axes fraction', fontsize=9, color='gray')
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/02_boxplot_comparativo.png", dpi=300)
    plt.close()

    # ── 3. Speed Up en tiempo virtual vs sequential ───────────────────────────
    # Y acotado cerca de 1.0 para que diferencias del 0.x% sean visibles.
    if 'sequential' in stats and stats['sequential']['vt_mean'] is not None:
        seq_vt = vt_mean_ms(stats['sequential'])
        other  = {l: stats[l] for l in labels if l != 'sequential'}
        if other:
            xlabels    = [l.upper() for l in other]
            speedups   = [seq_vt / vt_mean_ms(other[l]) for l in other]
            bar_colors = [colors.get(l, 'slategray') for l in other]
            sp_min = min(speedups)
            sp_max = max(speedups)
            sp_margin = max((sp_max - sp_min) * 0.5, 0.005)

            fig, ax = plt.subplots(figsize=(9, 6))
            bars = ax.bar(xlabels, speedups, color=bar_colors, alpha=0.8,
                          edgecolor='black', width=0.4)
            for bar, sv in zip(bars, speedups):
                pct = (sv - 1.0) * 100
                sign = '+' if pct >= 0 else ''
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f'{sv:.4f}x\n({sign}{pct:.2f}%)',
                        ha='center', va='bottom', fontsize=11, fontweight='bold')
            ax.axhline(1.0, color='royalblue', linestyle='--', linewidth=2,
                       label='Sequential (baseline = 1.0000×)')
            ax.set_ylim(max(0.98, sp_min - sp_margin), sp_max + sp_margin * 4)
            ax.set_ylabel('Speed Up en Tiempo Virtual (vs Sequential)', fontweight='bold')
            ax.set_title('Speed Up — Reloj Virtual\n'
                         '(eje Y acotado · independiente del scheduler del OS)',
                         fontweight='bold')
            ax.grid(True, alpha=0.3, axis='y')
            ax.legend()
            ax.annotate('⚠ Eje Y no empieza en 0', xy=(0.01, 0.02),
                        xycoords='axes fraction', fontsize=9, color='gray')
            plt.tight_layout()
            plt.savefig(f"{graphs_dir}/03_speedup_comparison.png", dpi=300)
            plt.close()

    # ── 4. Timeline: tiempo virtual por frame ────────────────────────────────
    # Eje Y acotado al rango real de los datos para que variaciones sean visibles.
    fig, ax = plt.subplots(figsize=(14, 6))
    for lbl, vt, c, avg in zip(labels, vt_all, clr, vt_avgs):
        frames = np.arange(1, len(vt) + 1)
        ax.plot(frames, vt, color=c, alpha=0.55, linewidth=0.9, label=lbl.upper())
        ax.axhline(avg, color=c, linestyle='--', linewidth=1.8)
        # Etiqueta del promedio al final de la línea
        ax.text(len(vt) + 1, avg, f' {avg:.3f} ms ({lbl.upper()})',
                va='center', fontsize=8, color=c, fontweight='bold')
    y_lo2 = vt_global_min - vt_margin * 4
    y_hi2 = vt_global_max + vt_margin * 4
    ax.set_ylim(y_lo2, y_hi2)
    ax.set_xlabel('Frame #', fontweight='bold')
    ax.set_ylabel('Tiempo Virtual por frame (ms)', fontweight='bold')
    ax.set_title('Tiempo Virtual por frame — Animación 200 frames\n'
                 '(eje Y acotado · línea discontinua = promedio)',
                 fontweight='bold')
    ax.legend(loc='upper left')
    ax.grid(True, alpha=0.3)
    ax.annotate('⚠ Eje Y no empieza en 0', xy=(0.01, 0.02),
                xycoords='axes fraction', fontsize=9, color='gray')
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/04_timeline_executions.png", dpi=300)
    plt.close()

    # ── 5. Tiempo Virtual promedio — doble panel (escala completa + overhead) ─
    # Panel superior: barras desde 0 (contexto general).
    # Panel inferior: overhead = VT_modelo − VT_mínimo, en μs (diferencias reales).
    # Esto hace visibles diferencias del orden de decenas de microsegundos.
    vt_min_val  = min(vt_avgs)
    overhead_us = [(v - vt_min_val) * 1e3 for v in vt_avgs]   # ms → μs

    fig, (ax_full, ax_oh) = plt.subplots(2, 1, figsize=(10, 10),
                                          gridspec_kw={'height_ratios': [1, 1.2]})

    # Panel superior: escala completa
    bars_full = ax_full.bar([l.upper() for l in labels], vt_avgs, color=clr,
                             alpha=0.8, edgecolor='black', width=0.45)
    for bar, v in zip(bars_full, vt_avgs):
        ax_full.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                     f'{v:.3f} ms', ha='center', va='bottom',
                     fontsize=11, fontweight='bold')
    ax_full.set_ylabel('Tiempo Virtual promedio por frame (ms)', fontweight='bold')
    ax_full.set_title('Tiempo Virtual promedio por modelo\n'
                      '(escala completa — mismo quantum · mismo costo de stall)',
                      fontweight='bold')
    ax_full.set_ylim(0, max(vt_avgs) * 1.12)
    ax_full.grid(True, alpha=0.3, axis='y')

    # Panel inferior: overhead sobre el mínimo en μs
    bars_oh = ax_oh.bar([l.upper() for l in labels], overhead_us, color=clr,
                         alpha=0.85, edgecolor='black', width=0.45)
    for bar, ov, lbl in zip(bars_oh, overhead_us, labels):
        label_txt = '0 μs\n(ideal)' if ov < 0.1 else f'+{ov:.1f} μs'
        ax_oh.text(bar.get_x() + bar.get_width() / 2,
                   max(bar.get_height(), max(overhead_us) * 0.02),
                   label_txt, ha='center', va='bottom',
                   fontsize=11, fontweight='bold')
    ax_oh.set_ylabel('Overhead sobre mínimo VT (μs)', fontweight='bold')
    ax_oh.set_title('Overhead de Tiempo Virtual respecto al modelo más rápido\n'
                    '(μs — diferencias reales de los ciclos de stall no ocultos)',
                    fontweight='bold')
    ax_oh.set_ylim(0, max(overhead_us) * 1.35 if max(overhead_us) > 0 else 1)
    ax_oh.grid(True, alpha=0.3, axis='y')
    # Nota aclaratoria
    best_lbl = labels[overhead_us.index(min(overhead_us))].upper()
    ax_oh.annotate(f'baseline = {best_lbl} ({vt_min_val:.3f} ms)',
                   xy=(0.01, 0.93), xycoords='axes fraction',
                   fontsize=9, color='gray')

    plt.tight_layout(pad=2.5)
    plt.savefig(f"{graphs_dir}/05_virtual_time_comparison.png", dpi=300, bbox_inches='tight')
    plt.close()

    # ── 6. CPU Speedup para modelos PERF (SMT/CMP) vs sequential ─────────────
    # Para FGMT/CGMT el CPU time no es la métrica → se excluyen de esta gráfica.
    if 'sequential' in stats:
        perf_labels = [l for l in labels if l in PERF_MODELS and stats[l]['mean'] > 0]
        if perf_labels:
            seq_cpu_mean = stats['sequential']['mean']
            cpu_speedups = [seq_cpu_mean / stats[l]['mean'] for l in perf_labels]
            bar_colors_p = [colors.get(l, 'slategray') for l in perf_labels]

            fig, ax = plt.subplots(figsize=(max(6, 4 * len(perf_labels)), 6))
            bars = ax.bar([l.upper() for l in perf_labels], cpu_speedups,
                          color=bar_colors_p, alpha=0.8, edgecolor='black', width=0.4)
            for bar, sv in zip(bars, cpu_speedups):
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f'{sv:.2f}x', ha='center', va='bottom',
                        fontsize=13, fontweight='bold')
            ax.axhline(1.0, color='royalblue', linestyle='--', linewidth=2,
                       label='Sequential (baseline = 1×)')
            ax.set_ylabel('Speed Up Tiempo CPU (vs Sequential)', fontweight='bold')
            ax.set_title('Speed Up Tiempo CPU — Modelos SMT/CMP\n'
                         '(throughput real · I/O excluido del timer)',
                         fontweight='bold')
            ax.set_ylim(0, max(max(cpu_speedups, default=1.0), 1.5) * 1.3)
            ax.grid(True, alpha=0.3, axis='y')
            ax.legend()
            plt.tight_layout()
            plt.savefig(f"{graphs_dir}/06_cpu_speedup_smt_cmp.png", dpi=300)
            plt.close()


# ─── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='Análisis de speed up multi-modelo')
    parser.add_argument('csvs', nargs='+',
                        help='CSVs a comparar: si hay 3, orden es seq fgmt cgmt; '
                             'si hay 2, orden es modelo sequential (legacy)')
    parser.add_argument('--graphs', default=None, help='Directorio para gráficas')
    parser.add_argument('--log', default=None, help='Archivo de log de salida')
    args = parser.parse_args()

    project_root = Path(__file__).parent.parent
    results_dir  = project_root / "results"
    log_file     = Path(args.log) if args.log else results_dir / "speedup_report.log"
    graphs_dir   = args.graphs if args.graphs else str(results_dir / "graficas")

    # Determinar qué CSVs se pasaron
    MODEL_NAMES = {
        'mediciones_secuencial': 'sequential',
        'mediciones_fgmt':       'fgmt',
        'mediciones_cgmt':       'cgmt',
        'mediciones_smt':        'smt',
        'mediciones_cmp':        'cmp',
    }

    if len(args.csvs) == 1:
        # legacy: solo modelo, busca sequential automáticamente
        csv_paths = {
            'sequential': str(results_dir / "mediciones_secuencial.csv"),
            Path(args.csvs[0]).stem.replace('mediciones_', ''): args.csvs[0],
        }
    elif len(args.csvs) == 2:
        # legacy: modelo + sequential
        stem0 = Path(args.csvs[0]).stem
        name0 = MODEL_NAMES.get(stem0, stem0.replace('mediciones_', ''))
        stem1 = Path(args.csvs[1]).stem
        name1 = MODEL_NAMES.get(stem1, stem1.replace('mediciones_', ''))
        csv_paths = {name0: args.csvs[0], name1: args.csvs[1]}
    else:
        # modo completo: seq fgmt cgmt (o más)
        csv_paths = {}
        for p in args.csvs:
            stem = Path(p).stem
            name = MODEL_NAMES.get(stem, stem.replace('mediciones_', ''))
            csv_paths[name] = p

    # Leer todos los CSVs
    stats = {}
    for name, path in csv_paths.items():
        s = analyze_csv(path)
        if s:
            stats[name] = s
        else:
            print(f"    ⚠ No se pudo leer: {path}")

    if not stats:
        print("Error: No se pudo leer ningún CSV.")
        sys.exit(1)

    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    SEP  = "=" * 68
    SEP2 = "-" * 68

    # ── consola y log ──────────────────────────────────────────────────────
    lines = []
    lines.append(SEP)
    lines.append("ANÁLISIS DE PERFORMANCE - SPEED UP COMPARATIVO")
    lines.append(SEP)
    lines.append(f"Timestamp : {timestamp}")
    lines.append(f"Modelos   : {', '.join(stats.keys())}")
    lines.append("")

    # ── Información de píxeles por modelo ─────────────────────────────────
    images_dir = project_root / 'results' / 'image'
    ppm_info = {}
    for name in stats:
        fname = PPM_FILENAME.get(name)
        if fname:
            ppm_path = images_dir / fname
            info = read_ppm_pixels(str(ppm_path))
            ppm_info[name] = (str(ppm_path), info)

    if ppm_info:
        lines.append("INFORMACIÓN DE IMÁGENES PPM")
        lines.append(SEP2)
        pixel_counts = set()
        for name, (path, info) in ppm_info.items():
            if info:
                w, h, total = info
                lines.append(f"  {name.upper():<12}: {w}x{h}  ({total:,} píxeles)  [{path}]")
                pixel_counts.add(total)
            else:
                lines.append(f"  {name.upper():<12}: archivo no encontrado  [{path}]")
        lines.append("")
        if len(pixel_counts) == 1:
            lines.append(f"  ✓ Todos los modelos coinciden en tamaño ({next(iter(pixel_counts)):,} píxeles)")
        elif len(pixel_counts) > 1:
            lines.append(f"  ✗ ADVERTENCIA: Los modelos difieren en cantidad de píxeles: {pixel_counts}")
        lines.append(SEP2)
        lines.append("")

    # ── Computar CPI y stalls promedio ────────────────────────────────────
    _NOP_NS = 100  # debe coincidir con NOP_PENALTY_NS en Constants.h
    for name, s in stats.items():
        ppm = ppm_info.get(name, (None, None))
        total_px = ppm[1][2] if (ppm[1] is not None) else (640 * 480)
        s['cpi'] = (s['vt_mean'] / (_NOP_NS * total_px)) if s['vt_mean'] is not None else None
        stall_data = s.get('stall_counts')
        s['avg_stalls'] = float(np.mean(stall_data)) if stall_data else None

    # Estadísticas individuales
    for name, s in stats.items():
        label = "SEQUENTIAL (Baseline)" if name == 'sequential' else name.upper()
        lines.append(format_stats(label, s))
        lines.append("")

    # Tabla comparativa de speed up vs sequential
    if 'sequential' in stats:
        seq_vt   = stats['sequential']['vt_mean']
        seq_mean = stats['sequential']['mean']
        lines.append(SEP2)
        lines.append("SPEED UP vs SEQUENTIAL")
        lines.append(SEP2)
        lines.append("  Nota metodológica:")
        lines.append("  • FGMT/CGMT: la métrica es el Tiempo Virtual (reloj de pipeline simulado).")
        lines.append("    CPU time = sobrecarga de emulación, no mide el rendimiento del scheduler.")
        lines.append("  • SMT/CMP : la métrica es el Tiempo CPU (throughput real del modelo hardware).")
        lines.append("  • I/O excluido del timer: los tiempos miden solo render_frame().")
        lines.append(SEP2)
        lines.append(f"  {'Modelo':<12} {'SpeedUp(VT)':<16} {'Status(VT)':<24} {'SpeedUp(CPU)':<16} {'Métrica usada'}")
        lines.append(f"  {'-'*12} {'-'*15} {'-'*23} {'-'*15} {'-'*20}")
        for name, s in stats.items():
            if name == 'sequential':
                continue
            cpu_ratio = seq_mean / s['mean']
            if seq_vt and s['vt_mean']:
                vt_ratio  = seq_vt / s['vt_mean']
                vt_str    = f"{vt_ratio:.2f}x"
                vt_status = speedup_status(vt_ratio)
            else:
                vt_str    = "N/A"
                vt_status = "N/A"
            is_perf = name.lower() in PERF_MODELS
            if is_perf:
                cpu_str    = f"{cpu_ratio:.2f}x"
                metrica    = "← CPU (throughput)"
            else:
                cpu_str    = f"({cpu_ratio:.2f}x emulación)"
                metrica    = "← VT (scheduler)"
            lines.append(f"  {name.upper():<12} {vt_str:<16} {vt_status:<24} {cpu_str:<16} {metrica}")
        lines.append(SEP2)
        lines.append("")

    # Tabla resumen horizontal
    lines.append("RESUMEN COMPARATIVO")
    lines.append(SEP2)
    header_cols = ['Métrica'] + [n.upper() for n in stats]
    col_w = 16
    lines.append("  " + "  ".join(f"{c:<{col_w}}" for c in header_cols))
    lines.append("  " + "  ".join("-"*col_w for _ in header_cols))

    def vt_ms(s, fn):
        if s['vt_times'] is not None:
            return f"{fn(s['vt_times'] / 1e6):.3f} ms"
        return "N/A"

    metrics_rows = [
        # Tiempo virtual (métrica principal)
        ('VT Promedio',    lambda s: vt_ms(s, lambda v: v.mean())),
        ('VT Mínimo',     lambda s: vt_ms(s, lambda v: v.min())),
        ('VT Máximo',     lambda s: vt_ms(s, lambda v: v.max())),
        ('VT Desv.Est.',   lambda s: vt_ms(s, lambda v: v.std())),
        # Tiempo CPU (referencia)
        ('CPU Promedio',   lambda s: f"{s['mean']:.6f} s"),
        ('CPU Mínimo',    lambda s: f"{s['min']:.6f} s"),
        ('CPU Máximo',    lambda s: f"{s['max']:.6f} s"),
        ('CPU Desv.Est.',  lambda s: f"{s['std']:.6f} s"),
        # Arquitectura
        ('Stalls Prom.',   lambda s: f"{s['avg_stalls']:.1f}"  if s.get('avg_stalls') is not None else 'N/A'),
        ('CPI',            lambda s: f"{s['cpi']:.4f}"          if s.get('cpi')        is not None else 'N/A'),
    ]
    for row_label, fn in metrics_rows:
        row = [f"{row_label:<{col_w}}"] + [f"{fn(stats[n]):<{col_w}}" for n in stats]
        lines.append("  " + "  ".join(row))
    lines.append(SEP2)
    lines.append("")
    lines.append(f"Archivos procesados:")
    for name, path in csv_paths.items():
        lines.append(f"  {name.upper():<12}: {path}")
    lines.append("")

    output = "\n".join(lines)
    print("\n" + output)

    with open(log_file, 'w') as f:
        f.write(output)

    # ── gráficas ───────────────────────────────────────────────────────────
    generate_graphs(stats, graphs_dir)


if __name__ == "__main__":
    main()
