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

# Número de hilos / contextos hardware de cada modelo.
# Usado para calcular Eficiencia Paralela: E = Speedup / N.
# (FGMT/CGMT: N=4 contextos compartiendo 1 pipeline; SMT: W=2 slots; CMP: N=4 cores)
THREAD_COUNT = {
    'sequential': 1,
    'fgmt':       4,    # 4 contextos hardware (pipeline compartido, VT simulado)
    'cgmt':       4,    # 4 contextos hardware (pipeline compartido, VT simulado)
    'smt':        2,    # W=2 issue slots sin OS threads (simulado)
    'cmp':        4,    # 4 OS threads reales (N = CMP_NUM_CORES)
}

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
    n_t   = len(times)
    # IC95% = media ± 1.96 × σ_muestral / √n  (distribución t → z para n≥30)
    ic95_half = 1.96 * times.std(ddof=1) / np.sqrt(n_t) if n_t > 1 else 0.0
    result = {
        'count':      n_t,
        'mean':       float(times.mean()),
        'median':     float(np.median(times)),
        'std':        float(times.std(ddof=1)),
        'min':        float(times.min()),
        'max':        float(times.max()),
        'ic95_low':   float(times.mean() - ic95_half),
        'ic95_high':  float(times.mean() + ic95_half),
        'ic95_half':  float(ic95_half),
        'times':      times,
    }
    if vt_rows:
        vt = np.array(vt_rows)
        vt_ic_half = 1.96 * vt.std(ddof=1) / np.sqrt(len(vt)) if len(vt) > 1 else 0.0
        result['vt_mean']      = int(vt.mean())
        result['vt_times']     = vt
        result['vt_ic95_low']  = float(vt.mean() - vt_ic_half)
        result['vt_ic95_high'] = float(vt.mean() + vt_ic_half)
        result['vt_ic95_half'] = float(vt_ic_half)
    else:
        result['vt_mean']      = None
        result['vt_times']     = None
        result['vt_ic95_low']  = None
        result['vt_ic95_high'] = None
        result['vt_ic95_half'] = None
    result['stall_counts'] = stall_rows if stall_rows else None
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
        lines.append(f"  Desv. Est. : {vt.std(ddof=1):.3f} ms")
        if s.get('vt_ic95_low') is not None:
            lines.append(f"  IC 95%     : [{s['vt_ic95_low']/1e6:.3f} ms,  {s['vt_ic95_high']/1e6:.3f} ms]")
    lines.append(f"  --- {cpu_label} ---")
    lines.append(f"  Promedio   : {s['mean']:.6f} s")
    lines.append(f"  Mediana    : {s['median']:.6f} s")
    lines.append(f"  Mínimo     : {s['min']:.6f} s")
    lines.append(f"  Máximo     : {s['max']:.6f} s")
    lines.append(f"  Desv. Est. : {s['std']:.6f} s")
    lines.append(f"  IC 95%     : [{s['ic95_low']:.6f} s,  {s['ic95_high']:.6f} s]")
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

    def pair_axis_limits(series_list):
        combined = np.concatenate(series_list)
        data_min = float(combined.min())
        data_max = float(combined.max())
        spread = data_max - data_min
        if spread < 1e-6:
            center = float(combined.mean())
            pad = max(center * 0.01, 0.001)
            return center - pad, center + pad
        pad = max(spread * 0.2, 0.001)
        return data_min - pad, data_max + pad

    def pair_hist_bins(series_list, x_lo, x_hi):
        combined = np.concatenate(series_list)
        span = x_hi - x_lo
        if span <= 0:
            return np.linspace(x_lo - 0.001, x_hi + 0.001, 9)
        if len(combined) < 2:
            return np.linspace(x_lo, x_hi, 9)
        q25, q75 = np.percentile(combined, [25, 75])
        iqr = q75 - q25
        if iqr > 0:
            bin_width = 2 * iqr * (len(combined) ** (-1 / 3))
            if bin_width > 0:
                n_bins = int(np.ceil(span / bin_width))
            else:
                n_bins = 12
        else:
            n_bins = 12
        n_bins = int(np.clip(n_bins, 8, 24))
        return np.linspace(x_lo, x_hi, n_bins + 1)

    def series_axis_limits(vt):
        data_min = float(vt.min())
        data_max = float(vt.max())
        spread = data_max - data_min
        if spread < 1e-9:
            center = float(vt.mean())
            pad = max(center * 0.005, 0.0005)
            return center - pad, center + pad
        pad = max(spread * 0.08, 0.0005)
        return data_min - pad, data_max + pad

    def series_hist_bins(vt, x_lo, x_hi):
        span = x_hi - x_lo
        unique_count = len(np.unique(vt))
        if span <= 0 or unique_count <= 1:
            return np.linspace(x_lo - 0.0005, x_hi + 0.0005, 10)
        n_bins = int(np.clip(max(12, unique_count), 12, 36))
        return np.linspace(x_lo, x_hi, n_bins + 1)

    vt_all  = [vt_series(stats[l]) for l in labels]
    vt_avgs = [vt_mean_ms(stats[l]) for l in labels]

    # Rango global de VT para boxplot y comparativas generales.
    vt_global_min = min(v.min() for v in vt_all)
    vt_global_max = max(v.max() for v in vt_all)
    vt_margin     = max((vt_global_max - vt_global_min) * 0.1, 0.001)

    # ── 1. Histogramas VT por pares comparables ──────────────────────────────
    # Se generan como archivos independientes para facilitar su uso en el paper.
    pair_specs = []
    if all(model in stats for model in ('fgmt', 'cgmt')):
        pair_specs.append(('FGMT vs CGMT', ['fgmt', 'cgmt'], '01_histogram_comparativo_gmt.png'))
    if all(model in stats for model in ('smt', 'cmp')):
        pair_specs.append(('SMT vs CMP', ['smt', 'cmp'], '01_histogram_comparativo_s-cmp.png'))
    if not pair_specs:
        pair_specs.append(('Comparación disponible', labels[:min(len(labels), 2)],
                           '01_histogram_comparativo.png'))

    for title, pair_labels, filename in pair_specs:
        fig, axes = plt.subplots(1, len(pair_labels), figsize=(7 * len(pair_labels), 5.5),
                                 sharex=False, sharey=False)
        if len(pair_labels) == 1:
            axes = [axes]

        for ax, lbl in zip(axes, pair_labels):
            vt = vt_series(stats[lbl])
            color = colors.get(lbl, 'slategray')
            x_lo, x_hi = series_axis_limits(vt)
            bins = series_hist_bins(vt, x_lo, x_hi)
            ic_h = 1.96 * vt.std(ddof=1) / np.sqrt(len(vt)) if len(vt) > 1 else 0
            ax.hist(vt, bins=bins, color=color, alpha=0.55, edgecolor='black',
                    linewidth=0.8, label=(f'μ={vt.mean():.3f} ms | '
                                           f'σ={vt.std(ddof=1):.4f} ms'))
            ax.axvline(vt.mean(), color=color, linestyle='--', linewidth=2)
            ax.axvspan(vt.mean() - ic_h, vt.mean() + ic_h, color=color, alpha=0.10)
            ax.set_xlim(x_lo, x_hi)
            ax.set_title(lbl.upper(), fontweight='bold', fontsize=13)
            ax.set_xlabel('Tiempo virtual por frame (ms)', fontsize=10)
            ax.set_ylabel('Frecuencia (frames)', fontsize=10)
            ax.legend(fontsize=9)
            ax.grid(True, alpha=0.3)

        plt.suptitle(title, fontweight='bold', fontsize=13)
        plt.tight_layout()
        plt.savefig(f"{graphs_dir}/{filename}", dpi=300, bbox_inches='tight')
        plt.close()

    # ── 2. Boxplots VT por pares comparables ──────────────────────────────────
    # Se agrupan igual que los histogramas para comparar familias afines.
    boxplot_specs = []
    for title, pair_labels, hist_filename in pair_specs:
        if hist_filename == '01_histogram_comparativo_gmt.png':
            boxplot_filename = '02_boxplot_comparativo_gmt.png'
        elif hist_filename == '01_histogram_comparativo_s-cmp.png':
            boxplot_filename = '02_boxplot_comparativo_s-cmp.png'
        else:
            boxplot_filename = '02_boxplot_comparativo.png'
        boxplot_specs.append((title, pair_labels, boxplot_filename))

    generated_hist_files = {filename for _, _, filename in pair_specs}
    generated_boxplot_files = {filename for _, _, filename in boxplot_specs}
    stale_outputs = []
    if '01_histogram_comparativo.png' not in generated_hist_files:
        stale_outputs.append('01_histogram_comparativo.png')
    if '02_boxplot_comparativo.png' not in generated_boxplot_files:
        stale_outputs.append('02_boxplot_comparativo.png')
    for stale_name in stale_outputs:
        stale_path = Path(graphs_dir) / stale_name
        if stale_path.exists():
            stale_path.unlink()

    for title, pair_labels, filename in boxplot_specs:
        pair_vt = [vt_series(stats[lbl]) for lbl in pair_labels]
        pair_colors = [colors.get(lbl, 'slategray') for lbl in pair_labels]

        fig, axes = plt.subplots(1, len(pair_labels), figsize=(6 * len(pair_labels), 5.5),
                                 sharex=False, sharey=False)
        if len(pair_labels) == 1:
            axes = [axes]

        for ax, lbl, vt, color in zip(axes, pair_labels, pair_vt, pair_colors):
            series_min = float(vt.min())
            series_max = float(vt.max())
            series_margin = max((series_max - series_min) * 0.1, 0.001)

            bp = ax.boxplot([vt], labels=[lbl.upper()], patch_artist=True, widths=0.45)
            bp['boxes'][0].set_facecolor(color)
            bp['boxes'][0].set_alpha(0.7)

            med_val = bp['medians'][0].get_ydata()[0]
            ax.text(1, med_val, f'  {med_val:.3f}', va='center',
                    fontsize=9, fontweight='bold', color='black')

            ax.set_ylim(series_min - series_margin * 3, series_max + series_margin * 3)
            ax.set_ylabel('Tiempo Virtual por frame (ms)', fontweight='bold')
            ax.set_title(lbl.upper(), fontweight='bold', fontsize=13)
            ax.grid(True, alpha=0.3, axis='y')

        plt.suptitle(title, fontweight='bold', fontsize=13)
        plt.tight_layout()
        plt.savefig(f"{graphs_dir}/{filename}", dpi=300, bbox_inches='tight')
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
            # IC95% del speedup propagado: σ_speedup ≈ (seq_VT / VT²) × σ_VT
            sp_ic95 = []
            for l in other:
                vt_arr = vt_series(other[l])
                if len(vt_arr) > 1:
                    se = vt_arr.std(ddof=1) / np.sqrt(len(vt_arr))
                    # δS/δVT = -seq_VT/VT²; IC propagado
                    sp_ic95.append(1.96 * se * (seq_vt / vt_mean_ms(other[l])**2))
                else:
                    sp_ic95.append(0.0)
            bars = ax.bar(xlabels, speedups, color=bar_colors, alpha=0.8,
                          edgecolor='black', width=0.4,
                          yerr=sp_ic95, capsize=5, error_kw={'ecolor':'black','elinewidth':1.5})
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
            ax.set_title('Speed Up — Reloj Virtual', fontweight='bold')
            ax.grid(True, alpha=0.3, axis='y')
            ax.legend()
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
    ax.set_title('Tiempo Virtual por frame — Animación 200 frames',
                 fontweight='bold')
    ax.legend(loc='upper left')
    ax.grid(True, alpha=0.3)
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
    ax_full.set_title('Tiempo Virtual promedio por modelo', fontweight='bold')
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
    ax_oh.set_title('Overhead de Tiempo Virtual respecto al modelo más rápido',
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
            ax.set_title('Speed Up Tiempo CPU — Modelos SMT/CMP',
                         fontweight='bold')
            ax.set_ylim(0, max(max(cpu_speedups, default=1.0), 1.5) * 1.3)
            ax.grid(True, alpha=0.3, axis='y')
            ax.legend()
            plt.tight_layout()
            plt.savefig(f"{graphs_dir}/06_cpu_speedup_smt_cmp.png", dpi=300)
            plt.close()

    # ── 7. Eficiencia Paralela: E = Speedup / N ───────────────────────────────
    # FGMT/CGMT: speedup de VT (reloj simulado), N=4 contextos.
    # SMT: speedup de CPU time, N=2 issue slots.
    # CMP: speedup de CPU time, N=4 cores reales.
    # E=1.0 → eficiencia perfecta (speedup = N); E<1.0 → overhead/contención.
    if 'sequential' in stats:
        seq_vt_avg = vt_mean_ms(stats['sequential'])
        seq_cpu_m  = stats['sequential']['mean']
        eff_data = []
        for lbl in labels:
            if lbl == 'sequential':
                continue
            n = THREAD_COUNT.get(lbl, 1)
            if lbl in PERF_MODELS:
                sp = seq_cpu_m / stats[lbl]['mean'] if stats[lbl]['mean'] > 0 else 0
            else:
                vm = vt_mean_ms(stats[lbl])
                sp = seq_vt_avg / vm if vm > 0 else 0
            eff_data.append((lbl, n, sp, sp / n if n > 0 else 0))
        if eff_data:
            xlabels_e  = [f"{d[0].upper()}\n(N={d[1]})" for d in eff_data]
            eff_values = [d[3] for d in eff_data]
            clr_e      = [colors.get(d[0], 'slategray') for d in eff_data]
            fig, ax = plt.subplots(figsize=(max(7, 3 * len(eff_data)), 6))
            bars = ax.bar(xlabels_e, eff_values, color=clr_e,
                          alpha=0.8, edgecolor='black', width=0.4)
            for bar, (lbl, n, sp, ef) in zip(bars, eff_data):
                sign = "+" if sp >= 1.0 else ""
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f'{ef:.3f}\n({ef*100:.1f}%)',
                        ha='center', va='bottom', fontsize=11, fontweight='bold')
            ax.axhline(1.0, color='gray', linestyle='--', linewidth=1.8,
                       label='Eficiencia ideal = 1.0 (100%)')
            ax.set_ylabel('Eficiencia Paralela  E = Speedup / N', fontweight='bold')
            ax.set_title('Eficiencia Paralela por Modelo', fontweight='bold')
            ax.set_ylim(0, max(max(eff_values, default=1.0), 1.05) * 1.30)
            ax.grid(True, alpha=0.3, axis='y')
            ax.legend()
            plt.tight_layout()
            plt.savefig(f"{graphs_dir}/07_efficiency.png", dpi=300)
            plt.close()

    # ── 8. Escalabilidad: N vs Speedup ───────────────────────────────────────
    # Curva ideal = speedup lineal (S = N).  Puntos reales muestran cuánto se
    # acerca cada modelo al ideal de Amdahl para fracción paralela ≈ 1.
    if 'sequential' in stats:
        seq_vt_avg = vt_mean_ms(stats['sequential'])
        seq_cpu_m  = stats['sequential']['mean']
        scale_pts  = [(1, 1.0, 'sequential', colors.get('sequential', 'royalblue'), 'baseline')]
        for lbl in labels:
            if lbl == 'sequential':
                continue
            n = THREAD_COUNT.get(lbl, 1)
            if lbl in PERF_MODELS:
                sp = seq_cpu_m / stats[lbl]['mean'] if stats[lbl]['mean'] > 0 else 0
                kind = 'real'
            else:
                vm = vt_mean_ms(stats[lbl])
                sp = seq_vt_avg / vm if vm > 0 else 0
                kind = 'modeled'
            scale_pts.append((n, sp, lbl, colors.get(lbl, 'slategray'), kind))

        if len(scale_pts) > 1:
            n_max = max(p[0] for p in scale_pts)
            n_rng = np.linspace(1, n_max * 1.2, 120)
            fig, ax = plt.subplots(figsize=(10, 6))
            ax.plot(n_rng, n_rng, 'k--', linewidth=1.5, alpha=0.4, label='Ideal S = N')
            for n_pt, sp_pt, lbl_pt, c_pt, kind_pt in scale_pts:
                marker = 'D' if kind_pt == 'baseline' else (
                         'o' if kind_pt == 'real' else 's')
                ax.scatter(n_pt, sp_pt, color=c_pt, s=170, zorder=5, marker=marker)
                offset = (8, 6) if lbl_pt != 'sequential' else (8, -16)
                ax.annotate(f'{lbl_pt.upper()}\n{sp_pt:.2f}x',
                            (n_pt, sp_pt), textcoords='offset points',
                            xytext=offset, fontsize=9, color=c_pt, fontweight='bold')
            ax.set_xlabel('Número de Hilos / Contextos (N)', fontweight='bold')
            ax.set_ylabel('Speedup vs Sequential', fontweight='bold')
            ax.set_title('Escalabilidad: Speedup en función de N',
                         fontweight='bold')
            ax.set_xlim(0, n_max * 1.35)
            y_top = max(p[1] for p in scale_pts)
            ax.set_ylim(0, max(y_top, float(n_max)) * 1.25)
            ax.grid(True, alpha=0.3)
            from matplotlib.lines import Line2D
            legend_elems = [
                Line2D([0], [0], marker='o', color='w', markerfacecolor='gray',
                       markersize=10, label='Real (CPU wall-clock)'),
                Line2D([0], [0], marker='s', color='w', markerfacecolor='gray',
                       markersize=10, label='Simulado (Tiempo Virtual)'),
                Line2D([0], [0], linestyle='--', color='k', alpha=0.4,
                       label='Speedup ideal = N'),
            ]
            ax.legend(handles=legend_elems)
            plt.tight_layout()
            plt.savefig(f"{graphs_dir}/08_scalability.png", dpi=300)
            plt.close()

    # ── 9. Grupo Simulado: Sequential vs FGMT vs CGMT — Tiempo Virtual ────────
    # Compara SOLO los modelos que simulan un pipeline compartido (misma métrica).
    # La métrica correcta es el Tiempo Virtual — el CPU time es overhead de emulación.
    MODELED_GROUP = [l for l in labels if l in ('sequential', 'fgmt', 'cgmt')]
    if len(MODELED_GROUP) >= 2:
        m_vt   = [vt_series(stats[l]) for l in MODELED_GROUP]
        m_avgs = [vt_mean_ms(stats[l]) for l in MODELED_GROUP]
        m_clrs = [colors.get(l, 'slategray') for l in MODELED_GROUP]

        fig, (ax_bp, ax_sp) = plt.subplots(1, 2, figsize=(14, 6))

        bp = ax_bp.boxplot(m_vt, labels=[l.upper() for l in MODELED_GROUP],
                           patch_artist=True, widths=0.5)
        for patch, c in zip(bp['boxes'], m_clrs):
            patch.set_facecolor(c); patch.set_alpha(0.7)
        for i, (med_line, lbl) in enumerate(zip(bp['medians'], MODELED_GROUP), start=1):
            mv = med_line.get_ydata()[0]
            ax_bp.text(i, mv, f' {mv:.3f}', va='center', fontsize=9, fontweight='bold')
        ax_bp.set_ylabel('Tiempo Virtual por frame (ms)', fontweight='bold')
        ax_bp.set_title('Distribución VT — Modelos Simulados',
                fontweight='bold')
        ax_bp.grid(True, alpha=0.3, axis='y')

        if 'sequential' in MODELED_GROUP:
            seq_vt_avg2 = vt_mean_ms(stats['sequential'])
            sp_vals = [seq_vt_avg2 / a if a > 0 else 0 for a in m_avgs]
            bars9 = ax_sp.bar([l.upper() for l in MODELED_GROUP], sp_vals,
                              color=m_clrs, alpha=0.8, edgecolor='black', width=0.45)
            for bar, sv in zip(bars9, sp_vals):
                ax_sp.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                           f'{sv:.3f}x', ha='center', va='bottom',
                           fontsize=12, fontweight='bold')
            ax_sp.axhline(1.0, color='royalblue', linestyle='--', linewidth=2,
                          label='Sequential = 1×')
            ax_sp.set_ylabel('Speed Up VT vs Sequential', fontweight='bold')
            ax_sp.set_title('Speed Up VT — Modelos Simulados\n(FGMT / CGMT vs Sequential)',
                            fontweight='bold')
            ax_sp.set_ylim(0, max(sp_vals) * 1.35)
            ax_sp.grid(True, alpha=0.3, axis='y')
            ax_sp.legend()

        plt.suptitle('Comparativa Modelos Simulados', fontweight='bold', fontsize=14)
        plt.tight_layout()
        plt.savefig(f"{graphs_dir}/09_modeled_group_comparison.png", dpi=300,
                    bbox_inches='tight')
        plt.close()

    # ── 10. Grupo Real: Sequential vs SMT vs CMP — CPU wall-clock ────────────
    # Compara SOLO los modelos con paralelismo real o baseline secuencial.
    # La métrica correcta es el CPU time (wall-clock sin I/O).
    REAL_GROUP = [l for l in labels if l in ('sequential', 'smt', 'cmp')]
    if len(REAL_GROUP) >= 2:
        r_times = [stats[l]['times'] * 1e3 for l in REAL_GROUP]   # s → ms
        r_avgs  = [stats[l]['mean']  * 1e3 for l in REAL_GROUP]
        r_clrs  = [colors.get(l, 'slategray') for l in REAL_GROUP]

        fig, (ax_bp2, ax_sp2) = plt.subplots(1, 2, figsize=(14, 6))

        bp2 = ax_bp2.boxplot(r_times, labels=[l.upper() for l in REAL_GROUP],
                             patch_artist=True, widths=0.5)
        for patch, c in zip(bp2['boxes'], r_clrs):
            patch.set_facecolor(c); patch.set_alpha(0.7)
        for i, (med_line, lbl) in enumerate(zip(bp2['medians'], REAL_GROUP), start=1):
            mv = med_line.get_ydata()[0]
            ax_bp2.text(i, mv, f' {mv:.3f}', va='center', fontsize=9, fontweight='bold')
        ax_bp2.set_ylabel('Tiempo CPU por frame (ms)', fontweight='bold')
        ax_bp2.set_title('Distribución CPU — Modelos Reales',
                 fontweight='bold')
        ax_bp2.grid(True, alpha=0.3, axis='y')

        if 'sequential' in REAL_GROUP:
            seq_cpu_m2 = stats['sequential']['mean'] * 1e3   # ms
            sp_vals2   = [seq_cpu_m2 / a if a > 0 else 0 for a in r_avgs]
            bars10 = ax_sp2.bar([l.upper() for l in REAL_GROUP], sp_vals2,
                                color=r_clrs, alpha=0.8, edgecolor='black', width=0.45)
            for bar, sv in zip(bars10, sp_vals2):
                ax_sp2.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                            f'{sv:.2f}x', ha='center', va='bottom',
                            fontsize=12, fontweight='bold')
            ax_sp2.axhline(1.0, color='royalblue', linestyle='--', linewidth=2,
                           label='Sequential = 1×')
            ax_sp2.set_ylabel('Speed Up CPU vs Sequential', fontweight='bold')
            ax_sp2.set_title('Speed Up CPU — Modelos Reales\n(SMT / CMP vs Sequential)',
                             fontweight='bold')
            ax_sp2.set_ylim(0, max(sp_vals2) * 1.35)
            ax_sp2.grid(True, alpha=0.3, axis='y')
            ax_sp2.legend()

        plt.suptitle('Comparativa Modelos Reales', fontweight='bold', fontsize=14)
        plt.tight_layout()
        plt.savefig(f"{graphs_dir}/10_real_group_comparison.png", dpi=300,
                    bbox_inches='tight')
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

    # ── Eficiencia paralela ────────────────────────────────────────────────
    if 'sequential' in stats:
        seq_vt_m2  = stats['sequential']['vt_mean']
        seq_cpu_m2 = stats['sequential']['mean']
        lines.append(SEP2)
        lines.append("EFICIENCIA PARALELA  (E = Speedup / N)")
        lines.append(SEP2)
        lines.append("  N = número de hilos/contextos del modelo.")
        lines.append("  FGMT/CGMT → Speedup de VT (reloj simulado).")
        lines.append("  SMT/CMP   → Speedup de CPU wall-clock.")
        lines.append(SEP2)
        hdr_e = f"  {'Modelo':<12} {'N':<5} {'Speedup':<12} {'Eficiencia':<14} {'Tipo'}"
        lines.append(hdr_e)
        lines.append(f"  {'-'*12} {'-'*4} {'-'*11} {'-'*13} {'-'*14}")
        for name, s in stats.items():
            if name == 'sequential':
                lines.append(f"  {'SEQUENTIAL':<12} {1:<5} {'1.0000':<12} {'1.0000':<14} baseline")
                continue
            n = THREAD_COUNT.get(name, 1)
            if name in PERF_MODELS:
                sp  = seq_cpu_m2 / s['mean'] if s['mean'] > 0 else 0
                tip = "CPU wall-clock"
            else:
                vm_ns = s['vt_mean']
                sp  = (seq_vt_m2 / vm_ns) if (seq_vt_m2 and vm_ns and vm_ns > 0) else 0
                tip = "Tiempo Virtual"
            eff = sp / n if n > 0 else 0
            lines.append(f"  {name.upper():<12} {n:<5} {sp:<12.4f} {eff:<14.4f} {tip}")
        lines.append(SEP2)
        lines.append("")

    # ── Escalabilidad ─────────────────────────────────────────────────────
    if 'sequential' in stats:
        lines.append(SEP2)
        lines.append("ESCALABILIDAD  (Speedup real vs Speedup ideal = N)")
        lines.append(SEP2)
        lines.append("  S_ideal = N (Amdahl con fracción paralela = 1).")
        lines.append(f"  {'Modelo':<12} {'N':<5} {'S_real':<12} {'S_ideal':<12} {'E=S/N':<10} {'Tipo'}")
        lines.append(f"  {'-'*12} {'-'*4} {'-'*11} {'-'*11} {'-'*9} {'-'*10}")
        for name, s in stats.items():
            n = THREAD_COUNT.get(name, 1)
            if name == 'sequential':
                lines.append(f"  {'SEQUENTIAL':<12} {1:<5} {'1.0000':<12} {'1.0000':<12} {'1.0000':<10} baseline")
                continue
            if name in PERF_MODELS:
                sp   = seq_cpu_m2 / s['mean'] if s['mean'] > 0 else 0
                tipo = "real"
            else:
                vm_ns = s['vt_mean']
                sp   = (seq_vt_m2 / vm_ns) if (seq_vt_m2 and vm_ns and vm_ns > 0) else 0
                tipo = "simulado"
            eff = sp / n if n > 0 else 0
            lines.append(f"  {name.upper():<12} {n:<5} {sp:<12.4f} {float(n):<12.4f} {eff:<10.4f} {tipo}")
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
        ('VT Mínimo',      lambda s: vt_ms(s, lambda v: v.min())),
        ('VT Máximo',      lambda s: vt_ms(s, lambda v: v.max())),
        ('VT Desv.Est.',   lambda s: vt_ms(s, lambda v: v.std(ddof=1))),
        # IC95% de VT
        ('VT IC95-bajo',   lambda s: f"{s['vt_ic95_low']/1e6:.3f} ms"  if s.get('vt_ic95_low')  is not None else 'N/A'),
        ('VT IC95-alto',   lambda s: f"{s['vt_ic95_high']/1e6:.3f} ms" if s.get('vt_ic95_high') is not None else 'N/A'),
        # Tiempo CPU (referencia)
        ('CPU Promedio',   lambda s: f"{s['mean']:.6f} s"),
        ('CPU Mínimo',     lambda s: f"{s['min']:.6f} s"),
        ('CPU Máximo',     lambda s: f"{s['max']:.6f} s"),
        ('CPU Desv.Est.',  lambda s: f"{s['std']:.6f} s"),
        ('CPU IC95-bajo',  lambda s: f"{s['ic95_low']:.6f} s"),
        ('CPU IC95-alto',  lambda s: f"{s['ic95_high']:.6f} s"),
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
