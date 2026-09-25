#!/usr/bin/env bash
# Replay extracted Moon frames through the camera centroid demo.
set -euo pipefail

demo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
frames_dir="$demo_root/assets/moon_test_moon_only_4x3"
centroid_model="$demo_root/assets/models/centroid/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx"
fps=2
loops=0
headless=false

usage() {
    cat <<'EOF'
Usage: scripts/run_moon_frames_loop.sh [--frames-dir DIR] [--model ONNX]
       [--fps N] [--loops N] [--headless]

Replay the Moon-only 4:3 frames with centroiding. --loops 0 (default) repeats
until Ctrl-C; --loops 1 runs once. The demo window restarts after each pass.
Use --frames-dir assets/moon_test_sky_4x3 to try the sky crop instead.
Set DEMO_BINARY_DIR=build/portable to use the locally rebuilt executable.
EOF
}

while (($#)); do
    case $1 in
        --frames-dir|--model|--fps|--loops)
            key=$1
            shift
            if (($# == 0)); then
                printf '[moon-loop][ERROR] Missing value for %s\n' "$key" >&2
                exit 2
            fi
            case $key in
                --frames-dir) frames_dir=$1 ;;
                --model) centroid_model=$1 ;;
                --fps) fps=$1 ;;
                --loops) loops=$1 ;;
            esac
            ;;
        --headless) headless=true ;;
        --help|-h) usage; exit 0 ;;
        *) printf '[moon-loop][ERROR] Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

[[ $loops =~ ^[0-9]+$ ]] || { printf '[moon-loop][ERROR] --loops must be a nonnegative integer\n' >&2; exit 2; }
[[ -d $frames_dir ]] || { printf '[moon-loop][ERROR] Missing frames directory: %s\n' "$frames_dir" >&2; exit 1; }
[[ -f $centroid_model ]] || { printf '[moon-loop][ERROR] Missing centroid model: %s\n' "$centroid_model" >&2; exit 1; }
frames_dir=$(realpath -- "$frames_dir")
centroid_model=$(realpath -- "$centroid_model")
shopt -s nullglob
frames=("$frames_dir"/*.png)
((${#frames[@]} > 0)) || { printf '[moon-loop][ERROR] No PNG frames in %s\n' "$frames_dir" >&2; exit 1; }

args=(--frames-dir "$frames_dir" --mode centroid --centroid-model "$centroid_model" --fps "$fps")
if $headless; then
    args+=(--headless)
fi

trap 'exit 130' INT
trap 'exit 143' TERM
pass=0
while ((loops == 0 || pass < loops)); do
    ((pass += 1))
    printf '[moon-loop][INFO] Starting pass %d with %d frames\n' "$pass" "${#frames[@]}"
    "$demo_root/scripts/run_local_bundle.sh" camera "${args[@]}"
done
