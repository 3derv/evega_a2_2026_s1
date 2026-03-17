# Proyecto Individual: Framework Experimental de Modelos de Ejecución Multithreading

## Descripción
Este proyecto implementa un framework experimental para comparar modelos de ejecución concurrente en un algoritmo de ray tracing altamente paralelizable. Se evalúan modelos como multihilo de grano fino, grano grueso, SMT y CMP, midiendo métricas de desempeño.

## Estructura del Proyecto
- `src/`: Código fuente en C++17
- `include/`: Archivos de cabecera
- `build/`: Archivos de compilación (CMake o Makefile)
- `docs/`: Documentación adicional
- `scripts/`: Scripts para mediciones y generación de gráficas
- `results/`: Resultados de pruebas y métricas
- `tests/`: Pruebas unitarias

## Reglas del Proyecto
- Lenguaje: C++17
- Algoritmo: Ray tracing
- Resolución: 640x480 píxeles
- Mediciones: Al menos 200 ejecuciones por configuración
- Herramientas: perf, VTune Profiler
- Git: Usar ramas master y development, commits incrementales
- Entregables: Código funcional, paper IEEE (máx 4 páginas), presentación

## Principios de Clean Code
Para mantener el código limpio, legible y mantenible, se aplicarán los siguientes principios:

### Principios SOLID
- **S (Single Responsibility Principle - SRP)**: Cada clase o módulo debe tener una sola responsabilidad. Por ejemplo, una clase debe encargarse únicamente de renderizar, no de manejar entrada/salida.
- **O (Open-Closed Principle - OCP)**: Las clases deben estar abiertas para extensión (agregar nuevas funcionalidades) pero cerradas para modificación (no cambiar código existente).
- **L (Liskov Substitution Principle - LSP)**: Los objetos de una subclase deben poder reemplazar a objetos de la clase base sin alterar el comportamiento esperado.
- **I (Interface Segregation Principle - ISP)**: Las interfaces deben ser específicas; no forzar a las clases a implementar métodos innecesarios.
- **D (Dependency Inversion Principle - DIP)**: Depender de abstracciones (interfaces) en lugar de implementaciones concretas para reducir acoplamiento.

### Principio DRY (Don't Repeat Yourself)
- Evitar duplicación de código. Si se repite lógica, extraerla a funciones, clases o módulos reutilizables. Por ejemplo, cálculos matemáticos comunes deben estar en utilidades compartidas.

### Regla Adicional
- **No usar emojis**: No se permiten emojis en el código, comentarios, commits, documentación o cualquier parte del proyecto. Mantener un estilo profesional y legible.

## Compilación y Ejecución
1. Clonar el repositorio
2. Compilar con CMake: `cmake .. && make` en build/
3. Ejecutar: `./raytracer` para versión secuencial
4. Para mediciones: `./raytracer --runs N` (N >= 200 recomendado)
5. Generar gráficas: `python3 scripts/generar_graficas.py results/mediciones_secuencial.csv`
6. Script completo: `./scripts/run_mediciones_secuencial.sh` (ejecuta 200 runs y genera gráficas)

## Metodología
- Desarrollo incremental con Git
- Ramas: master, development, feature branches
- Mediciones en hardware físico, no virtualizado
- Deshabilitar SMT para pruebas base

## Contacto
Profesor: Ronald García Fernández, Luis Chavarría Zamora