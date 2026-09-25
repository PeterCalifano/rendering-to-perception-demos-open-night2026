#!/usr/bin/env bash
# Copy the tested demo installation and its large inputs into ignored local folders.
set -euo pipefail

demo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
external_root="$demo_root/external"
asset_root="$demo_root/assets"

native_prefix=${NATIVE_PREFIX:-$demo_root/deps}
slam_prefix=${SLAM_PREFIX:-/home/peterc/devDir/SLAM-repos/slam-primitives/install}
ort_prefix=${ORT_PREFIX:-/home/peterc/devDir/ML-repos/onnxruntime/install}
opencv_prefix=${OPENCV_PREFIX:-/usr/local}
optix_root=${OPTIX_ROOT:-/home/peterc/devDir/dev-tools/nvidia/optix-sdk}
cuda_lib_dir=${CUDA_LIB_DIR:-/usr/local/cuda-12.9/targets/x86_64-linux/lib}
cudnn_lib_dir=${CUDNN_LIB_DIR:-/lib/x86_64-linux-gnu}
rendering_data=${RENDERING_DATA:-/media/peterc/SCRATCH_PRO/simulation_rendering_assets}
centroid_model=${CENTROID_MODEL:-/home/peterc/devDir/ML-repos/.worktrees/android-app-experiment/out/mobile/camera-model-input/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx}
yolo_root=${YOLO_ROOT:-/home/peterc/devDir/ML-repos/torchAutoForge-deploy}

require_file() {
    [[ -f $1 ]] || { printf 'Missing input: %s\n' "$1" >&2; exit 1; }
}

for input in "$demo_root/build/demo/render_stream_demo" \
             "$demo_root/build/demo/camera_stream_demo" \
             "$native_prefix/lib/libspectra_rt.so" \
             "$slam_prefix/lib/cmake/slam-primitives/slam-primitivesConfig.cmake" \
             "$ort_prefix/lib/libonnxruntime_providers_cuda.so" \
             "$opencv_prefix/lib/cmake/opencv4/OpenCVConfig.cmake" \
             "$optix_root/include/optix.h" \
             "$rendering_data/assets/bodies/bennu/shape/Bennu_17M_withMtl_split2.obj" \
             "$rendering_data/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg" \
             "$centroid_model" \
             "$yolo_root/examples/model_configs/yolov7_640x640.ptafmodel" \
             "$yolo_root/models/onnx/yolov7_640x640.onnx"; do
    require_file "$input"
done

if [[ -e $external_root || -e $asset_root ]]; then
    printf 'Refusing to mix a new bundle with existing external/ or assets/\n' >&2
    exit 1
fi

mkdir -p "$external_root" "$asset_root/rendering/assets/bodies/bennu/shape" \
         "$asset_root/rendering/assets/bodies/bennu/appearance/albedo" \
         "$asset_root/models/centroid" "$asset_root/models/yolo/examples/model_configs" \
         "$asset_root/models/yolo/models/onnx"

mkdir -p "$external_root/native" "$external_root/slam-primitives"
cp -a "$native_prefix/." "$external_root/native/"
cp -a "$slam_prefix/." "$external_root/slam-primitives/"
mkdir -p "$external_root/onnxruntime"
cp -a "$ort_prefix/include" "$ort_prefix/lib" "$external_root/onnxruntime/"
mkdir -p "$external_root/opencv/include" "$external_root/opencv/lib/cmake"
cp -a "$opencv_prefix/include/opencv4" "$external_root/opencv/include/"
cp -a "$opencv_prefix/lib/cmake/opencv4" "$external_root/opencv/lib/cmake/"
cp -a "$opencv_prefix/lib/opencv4" "$external_root/opencv/lib/"
cp -a "$opencv_prefix"/lib/libopencv*.so* "$external_root/opencv/lib/"
mkdir -p "$external_root/optix"
cp -a "$optix_root/include" "$external_root/optix/"
cp -a "$optix_root/license_info.txt" "$optix_root/OptiX_ThirdParty_Licenses.txt" \
      "$external_root/optix/"

# Keep the CUDA user-space runtime beside the copied ONNX CUDA provider.
# The NVIDIA driver and libcuda remain host-owned.
mkdir -p "$external_root/cuda-runtime/lib"
for library in libcudart libcublas libcublasLt libcufft libcurand libnvrtc libnvJitLink; do
    cp -a "$cuda_lib_dir/$library".so* "$external_root/cuda-runtime/lib/"
done
cp -a "$cudnn_lib_dir"/libcudnn*.so* "$external_root/cuda-runtime/lib/"

mkdir -p "$external_root/bin"
cp -a "$demo_root/build/demo/render_stream_demo" \
      "$demo_root/build/demo/camera_stream_demo" "$external_root/bin/"
cp -a "$rendering_data/assets/bodies/bennu/shape/Bennu_17M_withMtl_split2.obj" \
      "$asset_root/rendering/assets/bodies/bennu/shape/"
cp -a "$rendering_data/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg" \
      "$asset_root/rendering/assets/bodies/bennu/appearance/albedo/"
cp -a "$centroid_model" "$asset_root/models/centroid/"
cp -a "$yolo_root/examples/model_configs/yolov7_640x640.ptafmodel" \
      "$asset_root/models/yolo/examples/model_configs/"
cp -a "$yolo_root/models/onnx/yolov7_640x640.onnx" \
      "$asset_root/models/yolo/models/onnx/"

while IFS= read -r -d '' link_path; do
    link_target=$(realpath -e "$link_path") || {
        printf 'Broken bundle symlink: %s\n' "$link_path" >&2
        exit 1
    }
    case $link_target in
        "$external_root"/*|"$asset_root"/*) ;;
        *) printf 'Bundle symlink escapes its roots: %s\n' "$link_path" >&2; exit 1 ;;
    esac
done < <(find "$external_root" "$asset_root" -type l -print0)

(
    cd "$demo_root"
    find external assets -type f ! -name BUNDLE.sha256 -print0 |
        sort -z | xargs -0 sha256sum > external/BUNDLE.sha256
)

printf 'Bundle copied to %s and %s\n' "$external_root" "$asset_root"
