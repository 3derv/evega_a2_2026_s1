#!/usr/bin/env python3
"""
perf_to_json.py — Convierte los archivos perf_<modelo>.txt a un JSON estructurado.

Parsea la salida de 'perf stat' (hardware + software counters) y genera:
  results/perf/perf_results.json   ← todos los modelos en un solo archivo

Uso:
  python3 scripts/perf_to_json.py                     # procesa results/perf/*.txt
  python3 scripts/perf_to_json.py --perf-dir <ruta>   # directorio alternativo
  python3 scripts/perf_to_json.py --pretty             # JSON indentado (default: compacto)

También puede ser invocado desde run_perf.sh automáticamente al finalizar el perfilado.
"""

import argparse
import json
import os
import re
import sys
from datetime import datetime
from pathlib import Path


# ── Mapeo de nombre de evento perf → clave JSON limpia ───────────────────────
EVENT_ALIASES = {
    # hardware counters
    'cycles':               'cycles',
    'cycles:u':             'cycles',
    'instructions':         'instructions',
    'instructions:u':       'instructions',
    'cache-misses':         'cache_misses',
    'cache-misses:u':       'cache_misses',
    'cache-references':     'cache_references',
    'cache-references:u':   'cache_references',
    'branches':             'branches',
    'branches:u':           'branches',
    'branch-misses':        'branch_misses',
    'branch-misses:u':      'branch_misses',
    # software counters
    'task-clock':           'task_clock_ms',
    'task-clock:u':         'task_clock_ms',
    'context-switches':     'context_switches',
    'context-switches:u':   'context_switches',
    'cpu-migrations':       'cpu_migrations',
    'cpu-migrations:u':     'cpu_migrations',
    'page-faults':          'page_faults',
    'page-faults:u':        'page_faults',
}

# Patrón para líneas de contadores de perf stat:
#   valor [unidad]   evento_name    # comentario opcional
#   Ejemplos:
#     1,234,567      cycles:u           #  1.234 GHz
#        56627.89 msec task-clock:u     #  1.153 CPUs utilized
COUNTER_RE = re.compile(
    r'^\s*(?P<raw>[\d,\.]+)\s+(?P<unit>msec|M/sec|K/sec|sec)?\s*'
    r'(?P<event>\S+)\s*(?:#\s*(?P<comment>.*))?$'
)

# Patrón para el bloque de tiempo al final de perf stat:
#   49.118604617 seconds time elapsed
ELAPSED_RE  = re.compile(r'^\s*([\d\.]+)\s+seconds time elapsed')
USER_RE     = re.compile(r'^\s*([\d\.]+)\s+seconds user')
SYS_RE      = re.compile(r'^\s*([\d\.]+)\s+seconds sys')


def _parse_value(raw: str) -> float:
    """Convierte '1,234,567' o '56627.89' a float."""
    return float(raw.replace(',', ''))


def parse_perf_txt(filepath: str) -> dict:
    """
    Lee un archivo perf_<modelo>.txt y devuelve un diccionario con:
      - model: nombre del modelo
      - date: fecha de la ejecución
      - environment: 'wsl2' | 'linux_native' | 'unknown'
      - hw_available: bool — si los contadores hardware están disponibles
      - counters: dict con los valores de cada contador
      - derived: CPI, IPC, cache_miss_rate (calculados si los datos están disponibles)
      - timing: elapsed, user, sys (segundos)
      - simulation: VT avg, stalls avg, CPI simulado (extraídos del banner del binario)
    """
    if not os.path.exists(filepath):
        return {}

    model  = Path(filepath).stem.replace('perf_', '')
    result = {
        'model':       model,
        'source_file': str(filepath),
        'date':        None,
        'environment': 'unknown',
        'hw_available': False,
        'counters':    {},
        'derived':     {},
        'timing':      {},
        'simulation':  {},
    }

    with open(filepath, encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    for line in lines:
        stripped = line.strip()

        # ── Metadatos del banner ──────────────────────────────────────────
        if 'Fecha :' in stripped:
            m = re.search(r'Fecha\s*:\s*(.+)', stripped)
            if m:
                result['date'] = m.group(1).strip()

        if 'WSL2' in stripped or 'wsl' in stripped.lower():
            result['environment'] = 'wsl2'
        elif 'hardware físico' in stripped.lower() or 'linux native' in stripped.lower():
            result['environment'] = 'linux_native'

        if 'contadores hardware no disponibles' in stripped.lower():
            result['hw_available'] = False
        elif 'Contadores de hardware' in stripped and 'no disponibles' not in stripped.lower():
            result['hw_available'] = True

        # ── Métricas de simulación (del resumen del binario) ─────────────
        if 'Tiempo virtual (promedio):' in stripped:
            m = re.search(r'Tiempo virtual.*?:\s*([\d,\.]+)\s*ns', stripped)
            if m:
                result['simulation']['vt_avg_ns'] = _parse_value(m.group(1))

        if 'Tiempo promedio:' in stripped:
            m = re.search(r'Tiempo promedio:\s*([\d\.]+)\s*segundos', stripped)
            if m:
                result['simulation']['cpu_avg_s'] = float(m.group(1))

        if 'Stalls promedio' in stripped or 'Stalls Prom' in stripped:
            m = re.search(r'[Ss]talls.*?:\s*([\d\.]+)', stripped)
            if m:
                result['simulation']['stalls_avg'] = float(m.group(1))

        if re.match(r'\s*CPI\s*:', stripped):
            m = re.search(r'CPI\s*:\s*([\d\.]+)', stripped)
            if m:
                result['simulation']['cpi_simulated'] = float(m.group(1))

        if 'Número de ejecuciones:' in stripped:
            m = re.search(r'Número de ejecuciones:\s*(\d+)', stripped)
            if m:
                result['simulation']['num_runs'] = int(m.group(1))

        if 'Tiempo mínimo:' in stripped:
            m = re.search(r'Tiempo mínimo:\s*([\d\.]+)', stripped)
            if m:
                result['simulation']['cpu_min_s'] = float(m.group(1))

        if 'Tiempo máximo:' in stripped:
            m = re.search(r'Tiempo máximo:\s*([\d\.]+)', stripped)
            if m:
                result['simulation']['cpu_max_s'] = float(m.group(1))

        if 'Desviación estándar:' in stripped:
            m = re.search(r'Desviación estándar:\s*([\d\.]+)', stripped)
            if m:
                result['simulation']['cpu_std_s'] = float(m.group(1))

        # ── Contadores perf stat ──────────────────────────────────────────
        # Líneas de tiempo (bloque final)
        m_el = ELAPSED_RE.match(line)
        if m_el:
            result['timing']['elapsed_s'] = float(m_el.group(1))
            continue

        m_us = USER_RE.match(line)
        if m_us:
            result['timing']['user_s'] = float(m_us.group(1))
            continue

        m_sy = SYS_RE.match(line)
        if m_sy:
            result['timing']['sys_s'] = float(m_sy.group(1))
            continue

        # Líneas de contadores
        m_ctr = COUNTER_RE.match(line)
        if m_ctr:
            raw_val = m_ctr.group('raw')
            unit    = m_ctr.group('unit') or ''
            event   = m_ctr.group('event').strip()
            comment = (m_ctr.group('comment') or '').strip()
            try:
                value = _parse_value(raw_val)
            except ValueError:
                continue
            key = EVENT_ALIASES.get(event.lower(), event.lower().replace(':', '_').replace('-', '_'))
            result['counters'][key] = {
                'value':   value,
                'unit':    unit,
                'event':   event,
                'comment': comment,
            }

    # ── Métricas derivadas ────────────────────────────────────────────────
    c = result['counters']
    if 'cycles' in c and 'instructions' in c and c['instructions']['value'] > 0:
        cycles = c['cycles']['value']
        instrs = c['instructions']['value']
        result['derived']['CPI_hardware'] = round(cycles / instrs, 4)
        result['derived']['IPC_hardware'] = round(instrs / cycles, 4)

    if 'cache_misses' in c and 'cache_references' in c and c['cache_references']['value'] > 0:
        misses = c['cache_misses']['value']
        refs   = c['cache_references']['value']
        result['derived']['cache_miss_rate_pct'] = round(misses / refs * 100, 4)

    if 'branch_misses' in c and 'branches' in c and c['branches']['value'] > 0:
        result['derived']['branch_miss_rate_pct'] = round(
            c['branch_misses']['value'] / c['branches']['value'] * 100, 4
        )

    if 'task_clock_ms' in c:
        result['derived']['task_clock_ms'] = c['task_clock_ms']['value']

    # Marcar si el modelo tiene HW si encontramos ciclos
    if 'cycles' in c:
        result['hw_available'] = True

    return result


def main():
    parser = argparse.ArgumentParser(
        description='Convierte archivos perf_*.txt a JSON estructurado')
    parser.add_argument('--perf-dir', default=None,
                        help='Directorio con archivos perf_*.txt '
                             '(default: results/perf/ relativo al script)')
    parser.add_argument('--output', default=None,
                        help='Archivo JSON de salida '
                             '(default: <perf-dir>/perf_results.json)')
    parser.add_argument('--pretty', action='store_true', default=True,
                        help='JSON indentado (default: True)')
    args = parser.parse_args()

    project_root = Path(__file__).parent.parent
    perf_dir = Path(args.perf_dir) if args.perf_dir else project_root / 'results' / 'perf'

    if not perf_dir.exists():
        print(f"[perf_to_json] Directorio no encontrado: {perf_dir}", file=sys.stderr)
        sys.exit(1)

    txt_files = sorted(perf_dir.glob('perf_*.txt'))
    if not txt_files:
        print(f"[perf_to_json] No se encontraron archivos perf_*.txt en: {perf_dir}",
              file=sys.stderr)
        sys.exit(0)

    all_results = {}
    for txt_path in txt_files:
        model_data = parse_perf_txt(str(txt_path))
        if model_data:
            all_results[model_data['model']] = model_data
            print(f"  [perf_to_json] Procesado: {txt_path.name}")

    output_path = Path(args.output) if args.output else perf_dir / 'perf_results.json'
    payload = {
        'generated_at': datetime.now().isoformat(),
        'perf_dir':     str(perf_dir),
        'models':       all_results,
    }

    indent = 2 if args.pretty else None
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=indent, ensure_ascii=False)

    print(f"  [perf_to_json] JSON guardado en: {output_path}")
    print(f"  [perf_to_json] Modelos procesados: {list(all_results.keys())}")

    # Mostrar resumen de métricas derivadas en consola
    print()
    print("  Resumen métricas derivadas (hardware counters):")
    for model, data in all_results.items():
        derived = data.get('derived', {})
        sim     = data.get('simulation', {})
        env     = data.get('environment', 'unknown')
        hw_ok   = data.get('hw_available', False)
        print(f"    {model.upper():<12}  hw={hw_ok}  env={env}")
        if derived.get('CPI_hardware'):
            print(f"      CPI (HW)       : {derived['CPI_hardware']}")
        if derived.get('IPC_hardware'):
            print(f"      IPC (HW)       : {derived['IPC_hardware']}")
        if derived.get('cache_miss_rate_pct') is not None:
            print(f"      Cache miss rate: {derived['cache_miss_rate_pct']}%")
        if sim.get('cpi_simulated'):
            print(f"      CPI (simulado) : {sim['cpi_simulated']}")
        if sim.get('vt_avg_ns'):
            print(f"      VT avg         : {sim['vt_avg_ns']/1e6:.3f} ms")


if __name__ == '__main__':
    main()
