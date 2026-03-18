#!/usr/bin/env python3
"""
generar_graficas_fgmt.py: Generar gráficas estadísticas para mediciones FGMT

Uso:
    python3 scripts/generar_graficas_fgmt.py results/mediciones_fgmt.csv
    
Lee un CSV de mediciones y genera:
    - Histograma de distribución de tiempos
    - Boxplot para análisis estadístico
    - Imprime estadísticas descriptivas
"""

import pandas as pd
import matplotlib.pyplot as plt
import os
import sys

def main():
    # Obtener ruta del CSV desde argumentos
    if len(sys.argv) > 1:
        csv_file = sys.argv[1]
    else:
        csv_file = "./results/mediciones_fgmt.csv"
    
    # Validar que el archivo existe
    if not os.path.exists(csv_file):
        print(f"    ✗ No se encontró CSV: {csv_file}")
        sys.exit(1)
    
    # Directorio de gráficas
    graphs_dir = os.path.dirname(csv_file.replace("mediciones_", ""))
    graphs_dir = os.path.join(graphs_dir, "graficas")
    os.makedirs(graphs_dir, exist_ok=True)
    
    # Leer datos del CSV
    df = pd.read_csv(csv_file)
    if len(df) == 0:
        print("    ✗ CSV vacío")
        sys.exit(1)
    
    # Extraer tiempos (segunda columna)
    times = df.iloc[:, 1].values
    num_runs = len(times)
    
    # Determinar nombre del modelo desde la ruta del CSV
    model_name = csv_file.split("mediciones_")[1].split(".csv")[0] if "mediciones_" in csv_file else "modelo"
    
    # ========== HISTOGRAMA ==========
    plt.figure(figsize=(10, 6))
    plt.hist(times, bins=30, edgecolor='black', alpha=0.7, color='steelblue')
    plt.xlabel('Tiempo de ejecución (segundos)')
    plt.ylabel('Frecuencia')
    plt.title(f'Histograma de tiempos {model_name.upper()} ({num_runs} mediciones)')
    plt.grid(True, alpha=0.3)
    hist_path = os.path.join(graphs_dir, f"{model_name}_histogram.png")
    plt.tight_layout()
    plt.savefig(hist_path, dpi=150)
    print(f"    ✓ Histograma guardado: {hist_path}")
    
    # ========== BOXPLOT ==========
    plt.figure(figsize=(8, 6))
    plt.boxplot(times, vert=True)
    plt.ylabel('Tiempo de ejecución (segundos)')
    plt.title(f'Boxplot de tiempos {model_name.upper()} ({num_runs} mediciones)')
    plt.grid(True, alpha=0.3, axis='y')
    boxplot_path = os.path.join(graphs_dir, f"{model_name}_boxplot.png")
    plt.tight_layout()
    plt.savefig(boxplot_path, dpi=150)
    print(f"    ✓ Boxplot guardado: {boxplot_path}")
    
    plt.close('all')
    
    # ========== ESTADÍSTICAS ==========
    print(f"\n    Estadísticas {model_name.upper()}:")
    print(f"      Promedio: {times.mean():.6f} segundos")
    print(f"      Mínimo:   {times.min():.6f} segundos")
    print(f"      Máximo:   {times.max():.6f} segundos")
    print(f"      Desv.Est: {times.std():.6f} segundos")
    print(f"      Mediana:  {pd.Series(times).median():.6f} segundos")

if __name__ == "__main__":
    main()
