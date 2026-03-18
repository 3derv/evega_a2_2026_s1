#!/usr/bin/env python3
"""
Analizar speed up comparando Sequential vs FGMT (u otro modelo).
Lee CSV de mediciones, calcula estadísticas, y genera log.
"""

import sys
import os
from pathlib import Path
import pandas as pd
import numpy as np
from datetime import datetime

def analyze_csv(csv_path):
    """Leer CSV y calcular estadísticas."""
    if not os.path.exists(csv_path):
        return None
    
    df = pd.read_csv(csv_path)
    # Detectar nombre de columna de tiempo (puede ser "Tiempo(s)" o "time_ms")
    col_name = None
    for candidate in ['Tiempo(s)', 'time_ms', 'time_seconds']:
        if candidate in df.columns:
            col_name = candidate
            break
    
    if col_name is None:
        print(f"Error: No se encontró columna de tiempo en {csv_path}")
        print(f"Columnas disponibles: {df.columns.tolist()}")
        return None
    
    times = df[col_name].values
    # Si está en milisegundos, convertir a segundos
    if 'ms' in col_name.lower():
        times = times / 1000
    
    return {
        'count': len(times),
        'mean': times.mean(),
        'median': np.median(times),
        'std': times.std(),
        'min': times.min(),
        'max': times.max(),
    }

def format_stats(label, stats):
    """Formatear estadísticas para impresión."""
    if stats is None:
        return f"{label}: NO FOUND"
    
    lines = [
        f"{label}:",
        f"  Mediciones:  {stats['count']}",
        f"  Promedio:    {stats['mean']:.6f} segundos",
        f"  Mediana:     {stats['median']:.6f} segundos",
        f"  Mínimo:      {stats['min']:.6f} segundos",
        f"  Máximo:      {stats['max']:.6f} segundos",
        f"  Desv.Est:    {stats['std']:.6f} segundos",
    ]
    return "\n".join(lines)

def main():
    if len(sys.argv) < 2:
        print("Uso: python3 analizar_speedup.py <csv_model> [csv_sequential]")
        sys.exit(1)
    
    csv_model = sys.argv[1]
    csv_sequential = sys.argv[2] if len(sys.argv) > 2 else None
    
    project_root = Path(__file__).parent.parent
    results_dir = project_root / "results"
    log_file = results_dir / "speedup_report.log"
    
    # Si no se especifica CSV secuencial, asumir ubicación estándar
    if csv_sequential is None:
        csv_sequential = str(results_dir / "mediciones_secuencial.csv")
    
    # Leer CSV
    stats_model = analyze_csv(csv_model)
    stats_seq = analyze_csv(csv_sequential)
    
    # Obtener nombre del modelo del CSV
    csv_name = Path(csv_model).stem
    model_name = csv_name.replace("mediciones_", "").upper()
    
    # Imprimir en consola
    print("\n" + "="*60)
    print("ANÁLISIS DE PERFORMANCE - SPEED UP")
    print("="*60)
    print(f"Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print()
    
    print(format_stats("SEQUENTIAL (Baseline)", stats_seq))
    print()
    print(format_stats(model_name, stats_model))
    print()
    
    # Calcular speed up
    if stats_seq and stats_model:
        speedup = stats_seq['mean'] / stats_model['mean']
        slowdown_factor = stats_model['mean'] / stats_seq['mean']
        
        print("-" * 60)
        print(f"Speed Up ({model_name} vs SEQUENTIAL):")
        print(f"  Ratio:       {speedup:.2f}x")
        if speedup > 1.0:
            print(f"  Status:      ✓ FASTER (mejora de {(speedup-1)*100:.1f}%)")
        elif speedup < 1.0:
            print(f"  Status:      ✗ SLOWER (degradación de {(1-speedup)*100:.1f}%)")
        else:
            print(f"  Status:      = EQUAL")
        
        print()
        print(f"Desglose temporal:")
        print(f"  Secuencial:  {stats_seq['mean']:.6f}s")
        print(f"  {model_name:12s}: {stats_model['mean']:.6f}s")
        print(f"  Tiempo extra: {(stats_model['mean']-stats_seq['mean'])*1000:.3f} ms")
        print("-" * 60)
    else:
        print("⚠ No se pueden calcular estadísticas (archivos faltantes)")
    
    # Generar log
    with open(log_file, 'w') as f:
        f.write("="*60 + "\n")
        f.write("ANÁLISIS DE PERFORMANCE - SPEED UP\n")
        f.write("="*60 + "\n")
        f.write(f"Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("\n")
        
        f.write(format_stats("SEQUENTIAL (Baseline)", stats_seq) + "\n\n")
        f.write(format_stats(model_name, stats_model) + "\n\n")
        
        if stats_seq and stats_model:
            speedup = stats_seq['mean'] / stats_model['mean']
            f.write("-" * 60 + "\n")
            f.write(f"Speed Up ({model_name} vs SEQUENTIAL):\n")
            f.write(f"  Ratio:       {speedup:.2f}x\n")
            if speedup > 1.0:
                f.write(f"  Status:      FASTER (mejora de {(speedup-1)*100:.1f}%)\n")
            elif speedup < 1.0:
                f.write(f"  Status:      SLOWER (degradación de {(1-speedup)*100:.1f}%)\n")
            else:
                f.write(f"  Status:      EQUAL\n")
            
            f.write("\n")
            f.write(f"Desglose temporal:\n")
            f.write(f"  Secuencial:  {stats_seq['mean']:.6f}s\n")
            f.write(f"  {model_name:12s}: {stats_model['mean']:.6f}s\n")
            f.write(f"  Tiempo extra: {(stats_model['mean']-stats_seq['mean'])*1000:.3f} ms\n")
            f.write("-" * 60 + "\n")
        
        f.write(f"\nArchivos procesados:\n")
        f.write(f"  Secuencial:  {csv_sequential}\n")
        f.write(f"  {model_name:12s}: {csv_model}\n")
    
    print(f"\n✓ Log guardado en: {log_file}")
    print()

if __name__ == "__main__":
    main()
