#!/usr/bin/env bash
# Preview webcam KLT, centroiding, and YOLO until Esc is pressed.
set -euo pipefail

demo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-0}
export DEMO_BINARY_DIR=${DEMO_BINARY_DIR:-build/portable}
camera_index=${CAMERA_INDEX:-0}

exec "$demo_root/scripts/run_local_bundle.sh" camera \
    --camera-index "$camera_index" --mode both --klt-extraction generic \
    --yolo-model "$demo_root/assets/models/yolo/examples/model_configs/yolov7_640x640.ptafmodel" \
    "$@"
