#!/usr/bin/env python3
"""
run_thread_sweep.py  — Sweep de hilos N=2..8 para gráfica de escalabilidad.

Para cada N, actualiza Constants.h, recompila y ejecuta cada modelo.
Al final restaura Constants.h y genera results/graficas/08_scalability.png.

Uso:
    python3 scripts/run_thread_sweep.py [--threads 2,3,4,5,6,7,8]
"""

import argparse
import csv
import os
import re
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
CONSTANTS    = PROJECT_ROOT / "include" / "Constants.h"
BUILD_DIR    = PROJECT_ROOT / "build"
RAYTRACER    = BUILD_DIR / "raytracer"
RESULTS_DIR  = PROJECT_ROOT / "results"
SWEEP_CSV    = RESULTS_DIR / "thread_sweep.csv"
GRAPHS_DIR   = RESULTS_DIR / "graficas"

MODELS = ["sequential", "fgmt", "cgmt", "smt", "cmp"]


def update_threads(text: str, n: int) -> str:
    """Reemplaza NUM_THREADS, SMT_NUM_THREADS y CMP_NUM_CORES en Constants.h."""
    text = re.sub(r"(inline const int NUM_THREADS\s*=\s*)\d+;",
                  rf"\g<1>{n};", text)
    text = re.sub(r"(inline const int SMT_NUM_THREADS\s*=\s*)\d+;",
                  rf"\g<1>{n};", text)
    text = re.sub(r"(inline const int CMP_NUM_CORES\s*=\s*)\d+;",
                  rf"\g<1>{n};", text)
    return text


def rebuild() -> None:
    r = subprocess.run(["cmake", "--build", "build", "--", "-j4"],
                       cwd=str(PROJECT_ROOT), capture_output=True, text=True)
    if r.returncode != 0:
        print(f"[ERROR] Build falló:\n{r.stderr}", file=sys.stderr)
        sys.exit(1)


def run_model(model: str) -> float:
    """Corre el modelo con --runs 200 y devuelve VT promedio en ns."""
    r = subprocess.run([str(RAYTRACER), "--model", model, "--runs", "200"],
                       cwd=str(PROJECT_ROOT), capture_output=True, text=True,
                       timeout=600)
    if r.returncode != 0:
        print(f"[WARN] {model} retornó {r.returncode}", file=sys.stderr)
        return 0.0
    for line in r.stdout.splitlines():
        if "Tiempo virtual (promedio):" in line:
            # formato: "Tiempo virtual (promedio): 6109664 ns (6.110 ms)"
            m = re.search(r":\s*([\d.]+)\s*ns", line)
            if m:
                return float(m.group(1))
    return 0.0


def generate_graph(csv_path: Path, out_dir: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    rows = []
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)

    if not rows:
        print("[WARN] CSV vacío, sin gráfica")
        return

    models_in_data = sorted({r["model"] for r in rows if r["model"] != "sequential"})
    threads_set = sorted({int(r["threads"]) for r in rows})

    # VT sequential por N (debería ser similar para todo N)
    seq_vt = {}
    for r in rows:
        if r["model"] == "sequential":
            seq_vt[int(r["threads"])] = float(r["vt_avg_ns"])

    if not seq_vt:
        print("[WARN] No hay datos de sequential, sin gráfica")
        return

    colors = {
        "fgmt":  "seagreen",
        "cgmt":  "tomato",
        "smt":   "mediumpurple",
        "cmp":   "darkorange",
    }

    out_dir.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(10, 7))

    # Ideal line
    n_range = np.array(threads_set, dtype=float)
    ax.plot(n_range, n_range, "k--", linewidth=1.5, alpha=0.4, label="Ideal S = N")

    for model in models_in_data:
        pts = [(int(r["threads"]), float(r["vt_avg_ns"]))
               for r in rows if r["model"] == model]
        pts.sort()
        xs = [p[0] for p in pts]
        speedups = []
        for n, vt in pts:
            base = seq_vt.get(n, seq_vt.get(min(seq_vt.keys())))
            speedups.append(base / vt if vt > 0 else 0)

        ax.plot(xs, speedups, marker="o", linewidth=2, markersize=7,
                color=colors.get(model, "slategray"), label=model.upper())
        for x, sp in zip(xs, speedups):
            ax.annotate(f"{sp:.2f}x", (x, sp), textcoords="offset points",
                        xytext=(6, 6), fontsize=8, color=colors.get(model, "slategray"),
                        fontweight="bold")

    ax.set_xlabel("Número de hilos / contextos (N)", fontweight="bold")
    ax.set_ylabel("Speedup VT vs Sequential", fontweight="bold")
    ax.set_title("Escalabilidad: Speedup en función de N", fontweight="bold")
    ax.set_xticks(threads_set)
    ax.set_xlim(min(threads_set) - 0.5, max(threads_set) + 0.5)
    y_top = max(max(threads_set), 4) * 1.15
    ax.set_ylim(0, y_top)
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)
    plt.tight_layout()
    plt.savefig(str(out_dir / "08_scalability.png"), dpi=300, bbox_inches="tight")
    plt.close()
    print(f"[OK] Gráfica guardada: {out_dir / '08_scalability.png'}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Sweep de hilos para escalabilidad")
    parser.add_argument("--threads", default="2,3,4,5,6,7,8",
                        help="Lista de hilos separada por comas")
    args = parser.parse_args()

    thread_list = [int(x.strip()) for x in args.threads.split(",")]
    original_text = CONSTANTS.read_text(encoding="utf-8")

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    rows = []

    try:
        for n in thread_list:
            print(f"\n{'='*60}")
            print(f"  N = {n} hilos")
            print(f"{'='*60}")

            new_text = update_threads(original_text, n)
            CONSTANTS.write_text(new_text, encoding="utf-8")
            rebuild()

            for model in MODELS:
                print(f"  → {model:12s} ... ", end="", flush=True)
                vt = run_model(model)
                print(f"VT = {vt/1e6:.3f} ms")
                rows.append({"threads": n, "model": model, "vt_avg_ns": vt})
    finally:
        # Restaurar Constants.h siempre
        CONSTANTS.write_text(original_text, encoding="utf-8")
        rebuild()
        print("\n[OK] Constants.h restaurado y proyecto recompilado.")

    # Guardar CSV
    with SWEEP_CSV.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["threads", "model", "vt_avg_ns"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"[OK] CSV guardado: {SWEEP_CSV}")

    # Generar gráfica
    generate_graph(SWEEP_CSV, GRAPHS_DIR)


if __name__ == "__main__":
    main()
