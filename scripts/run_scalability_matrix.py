#!/usr/bin/env python3
"""
run_scalability_matrix.py

Ejecuta una matriz de experimentos para modelos de ejecucion con:
- sweep de tamano de imagen
- sweep de numero de hilos
- SMT hardware encendido/apagado
- perf stat por corrida

Salida:
- results/experiments_scaling/<timestamp>/summary.csv
- results/experiments_scaling/<timestamp>/plots/*.png
- resultados crudos por corrida (stdout, perf, csv)
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


EVENTS = [
    "cycles",
    "instructions",
    "cache-misses",
    "cache-references",
    "branches",
    "branch-misses",
    "task-clock",
    "context-switches",
    "cpu-migrations",
    "page-faults",
]

MODEL_CSV = {
    "sequential": "mediciones_secuencial.csv",
    "fgmt": "mediciones_fgmt.csv",
    "cgmt": "mediciones_cgmt.csv",
    "smt": "mediciones_smt.csv",
    "cmp": "mediciones_cmp.csv",
}

MODEL_TIMEOUT = {
    "sequential": 120,
    "fgmt": 480,
    "cgmt": 180,
    "smt": 240,
    "cmp": 180,
}


@dataclass
class RunConfig:
    smt_state: str
    width: int
    height: int
    threads: int
    model: str


def run_cmd(cmd: List[str], cwd: Path | None = None, timeout: int | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )


def require_tool(name: str) -> str:
    p = shutil.which(name)
    if not p:
        raise RuntimeError(f"Herramienta requerida no encontrada: {name}")
    return p


def parse_sizes(raw: str) -> List[Tuple[int, int]]:
    out = []
    for token in raw.split(","):
        token = token.strip()
        m = re.fullmatch(r"(\d+)x(\d+)", token)
        if not m:
            raise ValueError(f"Tamano invalido: {token}. Formato esperado: WxH")
        out.append((int(m.group(1)), int(m.group(2))))
    return out


def parse_int_list(raw: str) -> List[int]:
    return [int(x.strip()) for x in raw.split(",") if x.strip()]


def parse_models(raw: str) -> List[str]:
    models = [x.strip().lower() for x in raw.split(",") if x.strip()]
    valid = set(MODEL_CSV.keys())
    for m in models:
        if m not in valid:
            raise ValueError(f"Modelo invalido: {m}")
    return models


def update_constants(constants_path: Path, width: int, height: int, threads: int) -> None:
    text = constants_path.read_text(encoding="utf-8")

    def repl(pattern: str, replacement: str, src: str) -> str:
        if not re.search(pattern, src, flags=re.MULTILINE):
            raise RuntimeError(f"No se encontro patron en Constants.h: {pattern}")
        return re.sub(pattern, replacement, src, count=1, flags=re.MULTILINE)

    text = repl(r"inline const int IMAGE_WIDTH\s*=\s*\d+;", f"inline const int IMAGE_WIDTH  = {width};", text)
    text = repl(r"inline const int IMAGE_HEIGHT\s*=\s*\d+;", f"inline const int IMAGE_HEIGHT = {height};", text)
    text = repl(r"inline const int NUM_THREADS\s*=\s*\d+;", f"inline const int NUM_THREADS = {threads};", text)
    text = repl(r"inline const int SMT_NUM_THREADS\s*=\s*\d+;", f"inline const int SMT_NUM_THREADS = {threads};", text)
    text = repl(r"inline const int CMP_NUM_CORES\s*=\s*\d+;", f"inline const int CMP_NUM_CORES = {threads};", text)

    constants_path.write_text(text, encoding="utf-8")


def set_hw_smt(state: str, smt_ctrl: Path) -> bool:
    """Intenta cambiar el estado SMT hardware. Retorna True si tuvo éxito."""
    proc = run_cmd(["sudo", "-n", "bash", "-lc", f"echo {state} > {smt_ctrl}"])
    if proc.returncode != 0:
        return False
    return True


def get_hw_smt(smt_ctrl: Path) -> str:
    return smt_ctrl.read_text(encoding="utf-8").strip()


def detect_perf() -> str:
    candidates = ["perf", f"perf_{os.uname().release}"]
    candidates.extend(str(p) for p in Path("/usr/lib/linux-tools").glob("*/perf"))

    resolved: List[str] = []
    for c in candidates:
        if "/" in c:
            if Path(c).exists() and os.access(c, os.X_OK):
                resolved.append(c)
        else:
            w = shutil.which(c)
            if w:
                resolved.append(w)

    # Deduplicar conservando orden
    uniq = []
    seen = set()
    for c in resolved:
        if c not in seen:
            uniq.append(c)
            seen.add(c)

    for c in uniq:
        proc = run_cmd([c, "--version"])
        if proc.returncode == 0:
            return c
    raise RuntimeError("No se encontro perf funcional para este kernel")


def parse_perf_stat_x(perf_file: Path) -> Dict[str, float]:
    metrics: Dict[str, float] = {
        "cycles": 0.0,
        "instructions": 0.0,
        "cache-misses": 0.0,
        "cache-references": 0.0,
        "branches": 0.0,
        "branch-misses": 0.0,
        "task-clock": 0.0,
        "context-switches": 0.0,
        "cpu-migrations": 0.0,
        "page-faults": 0.0,
    }

    if not perf_file.exists():
        return metrics

    with perf_file.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            # perf -x, -> value,unit,event,...
            parts = [p.strip() for p in line.strip().split(",")]
            if len(parts) < 3:
                continue
            raw_val, unit, event = parts[0], parts[1], parts[2]
            if raw_val in ("<not counted>", "<not supported>", ""):
                continue

            try:
                value = float(raw_val)
            except ValueError:
                continue

            base_event = ""
            if "/" in event:
                # cpu_core/cycles/ -> cycles
                token = event.split("/")
                if len(token) >= 2:
                    base_event = token[1]
            else:
                base_event = event

            if base_event in metrics:
                metrics[base_event] += value

    return metrics


def parse_csv_metrics(csv_file: Path) -> Dict[str, float]:
    out = {
        "frames": 0.0,
        "cpu_avg_s": 0.0,
        "vt_avg_ns": 0.0,
        "stalls_avg": 0.0,
        "cpi_sim": 0.0,
    }
    if not csv_file.exists():
        return out

    times: List[float] = []
    vts: List[float] = []
    stalls: List[float] = []

    with csv_file.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            times.append(float(row["Tiempo(s)"]))
            vts.append(float(row["TiempoVirtual(ns)"]))
            stalls.append(float(row["Stalls"]))

    if not times:
        return out

    out["frames"] = float(len(times))
    out["cpu_avg_s"] = sum(times) / len(times)
    out["vt_avg_ns"] = sum(vts) / len(vts)
    out["stalls_avg"] = sum(stalls) / len(stalls)

    # CPI simulado definido en el proyecto
    # CPI = VT / (NOP_NS * pixels)
    # NOP_NS = 100
    # pixels = width * height (se setea luego en caller)
    return out


def append_summary_row(summary_path: Path, row: Dict[str, object]) -> None:
    exists = summary_path.exists()
    with summary_path.open("a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(row.keys()))
        if not exists:
            writer.writeheader()
        writer.writerow(row)


def ensure_native_linux() -> None:
    osrelease = Path("/proc/sys/kernel/osrelease").read_text(encoding="utf-8", errors="ignore").lower()
    if "microsoft" in osrelease or "wsl" in osrelease:
        print("[WARN] Entorno WSL detectado. Para validez del enunciado se recomienda Linux nativo.")


def generate_plots(summary_csv: Path, plots_dir: Path) -> None:
    try:
        import pandas as pd
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as e:
        print(f"[WARN] No se pudieron generar graficas (pandas/matplotlib): {e}")
        return

    plots_dir.mkdir(parents=True, exist_ok=True)
    df = pd.read_csv(summary_csv)
    if df.empty:
        print("[WARN] summary.csv vacio, sin graficas")
        return

    # Metricas derivadas
    df["pixels"] = df["image_w"] * df["image_h"]
    df["vt_ms"] = df["vt_avg_ns"] / 1e6
    df["cpu_ms"] = df["cpu_avg_s"] * 1e3
    df["ipc_hw"] = df.apply(
        lambda r: (r["instructions"] / r["cycles"]) if r["cycles"] > 0 else 0.0,
        axis=1,
    )
    df["cache_miss_rate"] = df.apply(
        lambda r: (r["cache_misses"] / r["cache_references"] * 100.0) if r["cache_references"] > 0 else 0.0,
        axis=1,
    )

    # 1) Speedup VT vs threads por modelo y estado SMT HW
    fig, ax = plt.subplots(figsize=(11, 7))
    base = df[df["model"] == "sequential"].copy()

    for (model, smt_state, pixels), g in df[df["model"].isin(["fgmt", "cgmt", "smt", "cmp"])].groupby([
        "model", "hw_smt", "pixels"
    ]):
        merged = g.merge(
            base[["hw_smt", "pixels", "threads", "vt_avg_ns"]],
            on=["hw_smt", "pixels", "threads"],
            suffixes=("", "_seq"),
            how="left",
        )
        if merged.empty:
            continue
        merged = merged.sort_values("threads")
        speedup = merged["vt_avg_ns_seq"] / merged["vt_avg_ns"]
        ax.plot(
            merged["threads"].to_numpy(),
            speedup.to_numpy(),
            marker="o",
            label=f"{model} | SMT-{smt_state} | {pixels}px",
        )

    ax.set_title("Speedup VT vs Sequential por hilos")
    ax.set_xlabel("Threads")
    ax.set_ylabel("Speedup (VT)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8, ncol=2)
    fig.tight_layout()
    fig.savefig(plots_dir / "01_speedup_vt_vs_threads.png", dpi=200)
    plt.close(fig)

    # 2) Comparativa solicitada seq/smt/cmp con SMT HW ON/OFF
    fig, ax = plt.subplots(figsize=(10, 6))
    target = df[df["model"].isin(["sequential", "smt", "cmp"])].copy()
    # promedio sobre todas las resoluciones/hilos por estado
    grouped = (
        target.groupby(["model", "hw_smt"], as_index=False)["cpu_ms"].mean().sort_values(["model", "hw_smt"])
    )

    x_labels = [f"{r.model}\nSMT-{r.hw_smt}" for r in grouped.itertuples(index=False)]
    ax.bar(x_labels, grouped["cpu_ms"].to_numpy(), color=["#3b82f6", "#60a5fa", "#f59e0b", "#fbbf24", "#10b981", "#34d399"]) 
    ax.set_title("CPU promedio: seq/smt/cmp con SMT HW ON/OFF")
    ax.set_ylabel("CPU por frame (ms)")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(plots_dir / "02_seq_smt_cmp_on_off_cpu.png", dpi=200)
    plt.close(fig)

    # 3) Escalabilidad CMP y SMT (CPU vs hilos)
    fig, ax = plt.subplots(figsize=(10, 6))
    for (model, smt_state, pixels), g in target[target["model"].isin(["smt", "cmp"])].groupby(["model", "hw_smt", "pixels"]):
        g = g.sort_values("threads")
        ax.plot(g["threads"].to_numpy(), g["cpu_ms"].to_numpy(), marker="o", label=f"{model} | SMT-{smt_state} | {pixels}px")

    ax.set_title("Escalabilidad CPU de SMT/CMP por hilos")
    ax.set_xlabel("Threads")
    ax.set_ylabel("CPU por frame (ms)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8, ncol=2)
    fig.tight_layout()
    fig.savefig(plots_dir / "03_scalability_cpu_smt_cmp.png", dpi=200)
    plt.close(fig)

    # 4) IPC vs cache miss (seq/smt/cmp)
    fig, ax = plt.subplots(figsize=(10, 6))
    perf_target = target.copy()
    for state, color in [("on", "#2563eb"), ("off", "#dc2626")]:
        g = perf_target[perf_target["hw_smt"] == state]
        ax.scatter(g["cache_miss_rate"].to_numpy(), g["ipc_hw"].to_numpy(), c=color, alpha=0.7, label=f"SMT-{state}")

    for r in perf_target.itertuples(index=False):
        ax.annotate(r.model, (r.cache_miss_rate, r.ipc_hw), fontsize=7, alpha=0.7)

    ax.set_title("Relacion IPC vs Cache-miss (seq/smt/cmp)")
    ax.set_xlabel("Cache miss rate (%)")
    ax.set_ylabel("IPC hardware")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(plots_dir / "04_ipc_vs_cachemiss_seq_smt_cmp.png", dpi=200)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description="Matriz de experimentos con perf + SMT on/off")
    parser.add_argument("--sizes", default="80x60,120x90,160x120", help="Lista WxH separada por comas")
    parser.add_argument("--threads", default="2,4", help="Lista de hilos separada por comas")
    parser.add_argument(
        "--models",
        default="sequential,fgmt,cgmt,smt,cmp",
        help="Modelos separados por comas",
    )
    parser.add_argument("--smt-states", default="on,off", help="Estados SMT hardware")
    parser.add_argument("--skip-perf", action="store_true", help="Omitir perf stat")
    parser.add_argument("--project-root", default=".", help="Raiz del proyecto")
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    ensure_native_linux()

    sizes = parse_sizes(args.sizes)
    threads = parse_int_list(args.threads)
    models = parse_models(args.models)
    smt_states = [s.strip().lower() for s in args.smt_states.split(",") if s.strip()]

    constants_path = project_root / "include" / "Constants.h"
    build_dir = project_root / "build"
    results_dir = project_root / "results"
    raytracer = build_dir / "raytracer"
    smt_ctrl = Path("/sys/devices/system/cpu/smt/control")

    if not constants_path.exists():
        raise RuntimeError(f"No existe {constants_path}")

    cmake = require_tool("cmake")
    python3_bin = require_tool("python3")
    perf_bin = ""
    if not args.skip_perf:
        perf_bin = detect_perf()

    # sudo check for SMT control
    if smt_ctrl.exists() and ("on" in smt_states or "off" in smt_states):
        chk = run_cmd(["sudo", "-n", "true"])
        if chk.returncode != 0:
            raise RuntimeError("Se requiere sudo cacheado. Ejecuta antes: sudo -v")

    original_constants = constants_path.read_text(encoding="utf-8")
    original_smt = get_hw_smt(smt_ctrl) if smt_ctrl.exists() else "unknown"

    ts = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    exp_dir = results_dir / "experiments_scaling" / ts
    raw_dir = exp_dir / "raw"
    csv_dir = raw_dir / "csv"
    perf_dir = raw_dir / "perf"
    out_dir = raw_dir / "stdout"
    plots_dir = exp_dir / "plots"

    for d in [csv_dir, perf_dir, out_dir, plots_dir]:
        d.mkdir(parents=True, exist_ok=True)

    summary_csv = exp_dir / "summary.csv"

    manifest = {
        "timestamp": ts,
        "sizes": sizes,
        "threads": threads,
        "models": models,
        "smt_states": smt_states,
        "skip_perf": args.skip_perf,
    }
    (exp_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print("[INFO] Inicio de matriz experimental")
    print(f"[INFO] Salida: {exp_dir}")

    run_id = 0
    try:
        for smt_state in smt_states:
            if smt_ctrl.exists():
                print(f"[INFO] Cambiando SMT hardware a: {smt_state}")
                if not set_hw_smt(smt_state, smt_ctrl):
                    print(f"[WARN] No se pudo fijar SMT={smt_state} (WSL2/VM). Saltando estado.")
                    continue
                now = get_hw_smt(smt_ctrl)
                if now != smt_state:
                    print(f"[WARN] SMT={smt_state} no se aplicó (actual={now}). Saltando estado.")
                    continue
            else:
                print("[WARN] SMT control no disponible; se continua con estado unico")

            for (w, h) in sizes:
                for th in threads:
                    print(f"[INFO] Configuracion: SMT={smt_state} | {w}x{h} | threads={th}")
                    update_constants(constants_path, w, h, th)

                    cfg = run_cmd([cmake, "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], cwd=project_root, timeout=120)
                    if cfg.returncode != 0:
                        raise RuntimeError(f"cmake configure fallo:\n{cfg.stderr}")

                    bld = run_cmd([cmake, "--build", "build", "--", "-j4"], cwd=project_root, timeout=600)
                    if bld.returncode != 0:
                        raise RuntimeError(f"build fallo:\n{bld.stderr}")

                    if not raytracer.exists():
                        raise RuntimeError("No se encontro build/raytracer tras compilar")

                    for model in models:
                        run_id += 1
                        run_name = f"run_{run_id:04d}_{model}_smt{smt_state}_{w}x{h}_t{th}"
                        stdout_file = out_dir / f"{run_name}.log"
                        perf_file = perf_dir / f"{run_name}.perf.csv"

                        cmd = [str(raytracer), "--model", model]
                        timeout = MODEL_TIMEOUT.get(model, 180)

                        if args.skip_perf:
                            proc = run_cmd(cmd, cwd=project_root, timeout=timeout)
                            stdout_file.write_text(proc.stdout + "\n\n" + proc.stderr, encoding="utf-8", errors="ignore")
                            perf_metrics = {k: 0.0 for k in [
                                "cycles", "instructions", "cache-misses", "cache-references",
                                "branches", "branch-misses", "task-clock", "context-switches",
                                "cpu-migrations", "page-faults"
                            ]}
                        else:
                            perf_cmd = [
                                perf_bin,
                                "stat",
                                "-x,",
                                "--no-big-num",
                                "-e",
                                ",".join(EVENTS),
                                "--",
                            ] + cmd
                            proc = subprocess.run(
                                perf_cmd,
                                cwd=str(project_root),
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                text=True,
                                timeout=timeout,
                                check=False,
                            )
                            stdout_file.write_text(proc.stdout, encoding="utf-8", errors="ignore")
                            perf_file.write_text(proc.stderr, encoding="utf-8", errors="ignore")
                            perf_metrics = parse_perf_stat_x(perf_file)

                        # Copiar CSV del modelo a archivo unico de esta corrida
                        src_csv = results_dir / MODEL_CSV[model]
                        dst_csv = csv_dir / f"{run_name}.csv"
                        if src_csv.exists():
                            shutil.copy2(src_csv, dst_csv)
                        else:
                            dst_csv.write_text("", encoding="utf-8")

                        csv_metrics = parse_csv_metrics(dst_csv)
                        pixels = w * h
                        cpi_sim = (csv_metrics["vt_avg_ns"] / (100.0 * pixels)) if pixels > 0 else 0.0

                        row = {
                            "run_id": run_id,
                            "hw_smt": smt_state,
                            "image_w": w,
                            "image_h": h,
                            "threads": th,
                            "model": model,
                            "exit_code": proc.returncode,
                            "frames": csv_metrics["frames"],
                            "cpu_avg_s": csv_metrics["cpu_avg_s"],
                            "vt_avg_ns": csv_metrics["vt_avg_ns"],
                            "stalls_avg": csv_metrics["stalls_avg"],
                            "cpi_sim": cpi_sim,
                            "cycles": perf_metrics["cycles"],
                            "instructions": perf_metrics["instructions"],
                            "cache_misses": perf_metrics["cache-misses"],
                            "cache_references": perf_metrics["cache-references"],
                            "branches": perf_metrics["branches"],
                            "branch_misses": perf_metrics["branch-misses"],
                            "task_clock_ms": perf_metrics["task-clock"],
                            "context_switches": perf_metrics["context-switches"],
                            "cpu_migrations": perf_metrics["cpu-migrations"],
                            "page_faults": perf_metrics["page-faults"],
                            "csv_file": str(dst_csv.relative_to(project_root)),
                            "perf_file": str(perf_file.relative_to(project_root)) if perf_file.exists() else "",
                            "stdout_file": str(stdout_file.relative_to(project_root)),
                        }
                        append_summary_row(summary_csv, row)
                        print(f"  [OK] {run_name} | exit={proc.returncode}")

        print("[INFO] Generando graficas...")
        generate_plots(summary_csv, plots_dir)

    finally:
        # Restaurar estado original
        constants_path.write_text(original_constants, encoding="utf-8")
        if smt_ctrl.exists() and original_smt in {"on", "off"}:
            run_cmd(["sudo", "-n", "bash", "-lc", f"echo {original_smt} > {smt_ctrl}"])

        # Reconstruir con constantes originales para no dejar el repo inestable
        run_cmd([cmake, "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], cwd=project_root)
        run_cmd([cmake, "--build", "build", "--", "-j4"], cwd=project_root)

    print("[INFO] Matriz finalizada")
    print(f"[INFO] summary: {summary_csv}")
    print(f"[INFO] plots:   {plots_dir}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.TimeoutExpired as e:
        print(f"[ERROR] Timeout en comando: {e}", file=sys.stderr)
        sys.exit(2)
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)
