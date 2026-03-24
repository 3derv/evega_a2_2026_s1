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
    lines.append(f"  --- Tiempo CPU (wall-clock, referencia) ---")
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
    colors = {'sequential': 'royalblue', 'fgmt': 'seagreen', 'cgmt': 'tomato'}
    clr    = [colors.get(l, 'gray') for l in labels]

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

    # ── 1. Histograma de tiempo virtual por modelo ───────────────────────────
    fig, axes = plt.subplots(1, len(labels), figsize=(5 * len(labels), 5))
    if len(labels) == 1:
        axes = [axes]
    for ax, lbl, vt, c in zip(axes, labels, vt_all, clr):
        ax.hist(vt, bins=25, color=c, alpha=0.75, edgecolor='black')
        ax.axvline(vt.mean(), color='black', linestyle='--', linewidth=1.5,
                   label=f'μ={vt.mean():.1f} ms')
        ax.set_title(lbl.upper(), fontweight='bold')
        ax.set_xlabel('Tiempo Virtual (ms)')
        ax.set_ylabel('Frecuencia')
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)
    plt.suptitle('Distribución del Tiempo Virtual por Modelo', fontweight='bold', y=1.02)
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/01_histogram_comparativo.png", dpi=150, bbox_inches='tight')
    plt.close()

    # ── 2. Boxplot de tiempo virtual ─────────────────────────────────────────
    fig, ax = plt.subplots(figsize=(8, 6))
    bp = ax.boxplot(vt_all, labels=[l.upper() for l in labels],
                    patch_artist=True, widths=0.5)
    for patch, c in zip(bp['boxes'], clr):
        patch.set_facecolor(c)
        patch.set_alpha(0.7)
    ax.set_ylabel('Tiempo Virtual (ms)', fontweight='bold')
    ax.set_title('Distribución del Tiempo Virtual\n(mismo quantum y costo de stall)', fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/02_boxplot_comparativo.png", dpi=150)
    plt.close()

    # ── 3. Speed Up en tiempo virtual vs sequential ──────────────────────────
    if 'sequential' in stats and stats['sequential']['vt_mean'] is not None:
        seq_vt = vt_mean_ms(stats['sequential'])
        other  = {l: stats[l] for l in labels if l != 'sequential'}
        if other:
            xlabels    = [l.upper() for l in other]
            speedups   = [seq_vt / vt_mean_ms(other[l]) for l in other]
            bar_colors = [colors.get(l, 'gray') for l in other]

            fig, ax = plt.subplots(figsize=(7, 5))
            bars = ax.bar(xlabels, speedups, color=bar_colors, alpha=0.8,
                          edgecolor='black', width=0.4)
            for bar, sv in zip(bars, speedups):
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f'{sv:.2f}x', ha='center', va='bottom',
                        fontsize=13, fontweight='bold')
            ax.axhline(1.0, color='royalblue', linestyle='--', linewidth=2,
                       label='Sequential (baseline = 1×)')
            ax.set_ylabel('Speed Up en Tiempo Virtual (vs Sequential)', fontweight='bold')
            ax.set_title('Speed Up — Reloj Virtual\n(independiente del scheduler del OS)', fontweight='bold')
            ax.set_ylim(0, max(speedups) * 1.3)
            ax.grid(True, alpha=0.3, axis='y')
            ax.legend()
            plt.tight_layout()
            plt.savefig(f"{graphs_dir}/03_speedup_comparison.png", dpi=150)
            plt.close()

    # ── 4. Timeline: tiempo virtual por run ──────────────────────────────────
    fig, ax = plt.subplots(figsize=(12, 5))
    for lbl, vt, c, avg in zip(labels, vt_all, clr, vt_avgs):
        runs = np.arange(1, len(vt) + 1)
        ax.plot(runs, vt, color=c, alpha=0.5, linewidth=0.8, label=lbl.upper())
        ax.axhline(avg, color=c, linestyle='--', linewidth=1.5)
    ax.set_xlabel('Run #', fontweight='bold')
    ax.set_ylabel('Tiempo Virtual (ms)', fontweight='bold')
    ax.set_title('Tiempo Virtual por run', fontweight='bold')
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/04_timeline_executions.png", dpi=150)
    plt.close()

    # ── 5. Barras comparativas: tiempo virtual promedio por modelo ───────────
    fig, ax = plt.subplots(figsize=(7, 5))
    bars = ax.bar([l.upper() for l in labels], vt_avgs, color=clr,
                  alpha=0.8, edgecolor='black', width=0.4)
    for bar, v in zip(bars, vt_avgs):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                f'{v:.1f} ms', ha='center', va='bottom',
                fontsize=11, fontweight='bold')
    ax.set_ylabel('Tiempo Virtual promedio (ms)', fontweight='bold')
    ax.set_title('Tiempo Virtual promedio por modelo\n(mismo quantum · mismo costo de stall)', fontweight='bold')
    ax.grid(True, alpha=0.3, axis='y')
    plt.tight_layout()
    plt.savefig(f"{graphs_dir}/05_virtual_time_comparison.png", dpi=150)
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
        lines.append(f"  {'Modelo':<12} {'SpeedUp(virtual)':<20} {'Status(virtual)':<24} {'SpeedUp(cpu)'}")
        lines.append(f"  {'-'*12} {'-'*19} {'-'*23} {'-'*12}")
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
            lines.append(f"  {name.upper():<12} {vt_str:<20} {vt_status:<24} {cpu_ratio:.2f}x")
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
