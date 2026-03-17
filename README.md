# Proyecto Individual: Framework Experimental de Modelos de Ejecución Multithreading

## ¿Qué encontrarás aquí?
Código en C++17 para comparar distintos modelos de ejecución concurrente usando ray tracing como problema base.

## Estructura mínima
- `src/`: Código fuente
- `include/`: Cabeceras y constantes
- `build/`: Archivos de compilación generados
- `docs/`: Documentación (reglas, guías, observaciones)
- `scripts/`: Scripts para mediciones y gráficas
- `results/`: Salidas de ejecución (CSV, imágenes, gráficas)

## Uso rápido
```bash
mkdir -p build && cd build
cmake .. && make
./raytracer --runs 200
```

Generar gráficas:
```bash
python3 scripts/generar_graficas.py results/mediciones_secuencial.csv
```

Ejecutar todas las mediciones secuenciales (200 runs + gráficas automáticas):
```bash
./scripts/run_mediciones_secuencial.sh
```

## Documentación y reglas del proyecto
Lee `docs/instructions.md` para ver las reglas del proyecto, buenas prácticas y cómo deben modelarse los distintos esquemas de ejecución.

## Notas rápidas
- Mantén el proyecto en un repositorio privado y usa ramas (`master`, `development`, feature branches).
- No uses emojis en código, commits ni documentación.
- Los resultados deben ser consistentes (misma salida para todos los modelos).
