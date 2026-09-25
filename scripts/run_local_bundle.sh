#!/usr/bin/env bash
# Launch copied binaries from the demo root with copied libraries first.
set -euo pipefail

demo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ $# -lt 1 || ( $1 != render && $1 != camera ) ]]; then
    printf 'Usage: %s {render|camera} [demo arguments...]\n' "$0" >&2
    exit 2
fi
program=$1
shift

binary_dir=${DEMO_BINARY_DIR:-$demo_root/external/bin}
if [[ $binary_dir != /* ]]; then
    binary_dir="$demo_root/$binary_dir"
fi
if [[ $program == render ]]; then
    executable="$binary_dir/render_stream_demo"
else
    executable="$binary_dir/camera_stream_demo"
fi
[[ -x $executable ]] || { printf 'Missing bundled executable: %s\n' "$executable" >&2; exit 1; }

for library in "$demo_root/external/native/lib/libspectra_rt.so" \
               "$demo_root/external/onnxruntime/lib/libonnxruntime.so.1" \
               "$demo_root/external/opencv/lib/libopencv_core.so.410" \
               "$demo_root/external/cuda-runtime/lib/libcudart.so.12"; do
    [[ -f $library ]] || { printf 'Missing bundled library: %s\n' "$library" >&2; exit 1; }
done

bundle_library_path="$demo_root/external/native/lib:$demo_root/external/onnxruntime/lib"
bundle_library_path+=":$demo_root/external/opencv/lib:$demo_root/external/cuda-runtime/lib"
export LD_LIBRARY_PATH="$bundle_library_path${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export RENDERING_DATA="$demo_root/assets/rendering"
cd "$demo_root"
exec "$executable" "$@"
