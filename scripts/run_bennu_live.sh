#!/usr/bin/env bash
# Preview textured Bennu with KLT and centroiding until Esc is pressed.
set -euo pipefail

demo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-0}
export DEMO_BINARY_DIR=${DEMO_BINARY_DIR:-build/portable}

exec "$demo_root/scripts/run_local_bundle.sh" render \
    --scene bennu --mode both \
    --centroid-model "$demo_root/assets/models/centroid/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx" \
    --albedo-jpeg "$demo_root/assets/rendering/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg" \
    --spp 8 --exposure-ms 1.5 "$@"
