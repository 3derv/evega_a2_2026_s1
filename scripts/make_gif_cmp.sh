#!/usr/bin/env bash
# make_gif_cmp.sh — Ensambla gif_cmp.gif a partir de los 200 frames PPM
# del modelo CMP (4 cores, paralelismo real) con órbita elíptica de cámara.
#
# Flujo:
#   1. Si los 200 frames ya existen en tests/gif_utils_cmp/ → los reutiliza.
#   2. Si faltan    → ejecuta ./build/raytracer --model cmp --gif para generarlos.
#   3. Ensambla el GIF con FFmpeg (si está disponible) o Pillow como fallback.
#
# Uso:   ./scripts/make_gif_cmp.sh
# Salida: results/gif/gif_cmp.gif
# Frames: tests/gif_utils_cmp/frame_0000.ppm … frame_0199.ppm
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
cd "$ROOT"

FRAMES_DIR="tests/gif_utils_cmp"
GIF_DIR="results/gif"
GIF_OUT="$GIF_DIR/gif_cmp.gif"
EXPECTED_FRAMES=200

# ── 1. Verificar herramientas disponibles ─────────────────────────────────────
HAS_FFMPEG=0
HAS_PILLOW=0
command -v ffmpeg &>/dev/null && HAS_FFMPEG=1
python3 -c "from PIL import Image" &>/dev/null && HAS_PILLOW=1

if [[ $HAS_FFMPEG -eq 0 && $HAS_PILLOW -eq 0 ]]; then
    echo "ERROR: ni ffmpeg ni Pillow (Python) están disponibles."
    echo "  Instala ffmpeg:  sudo apt install ffmpeg"
    echo "  o Pillow:        pip install Pillow"
    exit 1
fi

# ── 2. Renderizar solo si faltan frames ───────────────────────────────────────
FOUND=$(find "$FRAMES_DIR" -name "frame_*.ppm" 2>/dev/null | wc -l || true)
if [[ $FOUND -ge $EXPECTED_FRAMES ]]; then
    echo "[1/2] Frames ya existentes ($FOUND encontrados) — omitiendo render."
else
    echo "[1/2] Renderizando $EXPECTED_FRAMES frames (cmp, 80x60)..."
    mkdir -p "$FRAMES_DIR" "$GIF_DIR"
    ./build/raytracer --model cmp --gif
    echo "      Frames guardados en $FRAMES_DIR"
fi

# ── 3. Ensamblar GIF ──────────────────────────────────────────────────────────
echo "[2/2] Generando GIF…"

if command -v ffmpeg &>/dev/null; then
    # FFmpeg: 2-pass con paleta óptima (mejor calidad)
    PALETTE="$GIF_DIR/.palette_cmp.png"
    ffmpeg -y -loglevel warning \
        -framerate 24 \
        -i "$FRAMES_DIR/frame_%04d.ppm" \
        -vf "scale=160:120:flags=neighbor,palettegen=max_colors=256:stats_mode=full" \
        "$PALETTE"
    ffmpeg -y -loglevel warning \
        -framerate 24 \
        -i "$FRAMES_DIR/frame_%04d.ppm" \
        -i "$PALETTE" \
        -lavfi "scale=160:120:flags=neighbor[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3" \
        "$GIF_OUT"
    rm -f "$PALETTE"
else
    echo "      ffmpeg no encontrado — usando Pillow (Python)…"
    python3 - <<PYEOF
import glob, os
from PIL import Image

def load_ppm(path):
    """Lee un PPM P3 (ASCII) y retorna un PIL Image RGB."""
    with open(path, 'r') as f:
        assert f.readline().strip() == 'P3', "Solo se soporta PPM P3"
        w, h = map(int, f.readline().split())
        int(f.readline())        # maxval (ignorado, asumimos 255)
        data = list(map(int, f.read().split()))
    return Image.frombytes('RGB', (w, h), bytes(data))

frames_dir  = "$FRAMES_DIR"
gif_out     = "$GIF_OUT"
fps         = 24
duration_ms = int(1000 / fps)

paths = sorted(glob.glob(os.path.join(frames_dir, "frame_*.ppm")))
if not paths:
    raise SystemExit(f"No se encontraron frames en {frames_dir}")

# Convertir a paleta para GIF (necesario para el formato GIF)
imgs = [load_ppm(p).convert("P", palette=Image.ADAPTIVE, colors=256) for p in paths]
imgs[0].save(
    gif_out,
    save_all=True,
    append_images=imgs[1:],
    loop=0,
    duration=duration_ms,
    optimize=False,
)
print(f"      {len(imgs)} frames ensamblados.")
PYEOF
fi

SIZE=$(du -sh "$GIF_OUT" | cut -f1)
echo ""
echo "GIF generado: $GIF_OUT  ($SIZE)"
