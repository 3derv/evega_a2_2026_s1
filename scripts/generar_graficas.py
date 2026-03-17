#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import sys
import os

# Verificar argumentos
if len(sys.argv) < 2:
    print("Uso: python generar_graficas.py <archivo_csv>")
    sys.exit(1)

csv_file = sys.argv[1]
if not os.path.exists(csv_file):
    print(f"Archivo {csv_file} no encontrado.")
    sys.exit(1)

# Leer datos
data = pd.read_csv(csv_file)

# Crear directorio para gráficas
output_dir = os.path.join(os.path.dirname(csv_file), "graficas")
os.makedirs(output_dir, exist_ok=True)

# Histograma
plt.figure(figsize=(10, 6))
plt.hist(data['Tiempo(s)'], bins=20, edgecolor='black')
plt.title('Distribución de Tiempos de Ejecución (Secuencial)')
plt.xlabel('Tiempo (s)')
plt.ylabel('Frecuencia')
plt.savefig(os.path.join(output_dir, 'histograma_secuencial.png'))
plt.close()

# Boxplot
plt.figure(figsize=(8, 6))
plt.boxplot(data['Tiempo(s)'])
plt.title('Boxplot de Tiempos de Ejecución (Secuencial)')
plt.ylabel('Tiempo (s)')
plt.savefig(os.path.join(output_dir, 'boxplot_secuencial.png'))
plt.close()

print(f"Gráficas generadas en {output_dir}")