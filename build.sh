#!/usr/bin/env bash
# Configure and build both demos against the copied local dependencies.
set -euo pipefail

demo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$demo_root/build/portable"
jobs=8
build_tests=OFF

usage() {
    cat <<'EOF'
Usage: ./build.sh [--jobs N] [--tests]

Configure and build both demos with centroiding and YOLO support using external/.
--tests also builds and runs the camera stream behavior test (Python 3.12 required).

Copy external/ and assets/ with the repository before building on another host.
EOF
}

while (($#)); do
    case $1 in
        --jobs)
            if (($# < 2)) || [[ ! $2 =~ ^[1-9][0-9]*$ ]]; then
                printf 'Expected a positive integer after --jobs\n' >&2
                exit 2
            fi
            jobs=$2
            shift 2
            ;;
        --tests)
            build_tests=ON
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

for tool in cmake ninja; do
    command -v "$tool" >/dev/null || {
        printf 'Missing build tool: %s\n' "$tool" >&2
        exit 1
    }
done

for input in \
    "$demo_root/external/native/lib/cmake/spectra_rt/spectra_rtConfig.cmake" \
    "$demo_root/external/native/lib/cmake/pyramidal_klt/pyramidal_kltConfig.cmake" \
    "$demo_root/external/native/lib/cmake/autoforge_deploy/autoforge_deployConfig.cmake" \
    "$demo_root/external/slam-primitives/lib/cmake/slam-primitives/slam-primitivesConfig.cmake" \
    "$demo_root/external/onnxruntime/lib/cmake/onnxruntime/onnxruntimeConfig.cmake" \
    "$demo_root/external/opencv/lib/cmake/opencv4/OpenCVConfig.cmake" \
    "$demo_root/external/optix/include/optix.h"; do
    [[ -f $input ]] || {
        printf 'Missing copied build dependency: %s\n' "$input" >&2
        printf 'Copy external/ from the prepared demo directory before building\n' >&2
        exit 1
    }
done

prefix_path="$demo_root/external/native;$demo_root/external/slam-primitives"
prefix_path+=";$demo_root/external/onnxruntime"
cmake -S "$demo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$prefix_path" \
    -DOpenCV_DIR="$demo_root/external/opencv/lib/cmake/opencv4" \
    -DOPTIX_ROOT="$demo_root/external/optix" \
    -DDEMO_ENABLE_ML=ON -DDEMO_BUILD_TESTS="$build_tests"
cmake --build "$build_dir" --parallel "$jobs"

if [[ $build_tests == ON ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
fi

printf 'Built both demos in %s\n' "$build_dir"
printf 'Run with DEMO_BINARY_DIR=build/portable scripts/run_local_bundle.sh {render|camera} ...\n'
