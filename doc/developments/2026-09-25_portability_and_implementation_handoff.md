# Rendering-to-perception demo: portability and implementation handoff

Snapshot: 2026-09-25. This report describes the working installation on Ubuntu
24.04 and the files needed to reproduce it elsewhere. The ten existing demo
commit titles were reworded and re-signed in both local checkouts; their file
trees did not change. Both GitHub-tracking refs still point to the old history.
Read [AGENTS.md](../../AGENTS.md), [PLAN.md](../../PLAN.md), and
[README.md](../../README.md) before changing code. The ignored `external/` and
`assets/` directories are now the copied runtime bundle; a Git clone alone does
not contain them.

## Local copy bundle

Run [package_local_bundle.sh](../../scripts/package_local_bundle.sh) once from
this demo checkout after building `build/demo`. It copies the installed native
libraries and executables into ignored `external/`, and Bennu plus the centroid
and YOLO model inputs into ignored `assets/`. Its defaults name the source
paths in the provenance section below. Override `NATIVE_PREFIX`, `SLAM_PREFIX`,
`ORT_PREFIX`, `OPENCV_PREFIX`, `OPTIX_ROOT`, `CUDA_LIB_DIR`, `CUDNN_LIB_DIR`,
`RENDERING_DATA`, `CENTROID_MODEL`, or `YOLO_ROOT` before running it if an
installation moved. It refuses to mix a new bundle with existing directories;
inspect or move an older bundle before packaging again.

The folder contract is:

```text
external/bin/                built demo executables
external/native/             Spectra-RT, KLT, AutoForge installed prefix
external/slam-primitives/    installed headers and CMake package
external/onnxruntime/       ONNX Runtime headers, CMake package, CPU/CUDA libraries
external/opencv/            OpenCV 4.10 headers, CMake package, shared/3rdparty libraries
external/optix/             OptiX 9 headers and license files for compilation
external/cuda-runtime/lib/  CUDA 12.9 and cuDNN 9 user-space shared libraries
external/BUNDLE.sha256      checksums for regular files in external/ and assets/
assets/rendering/assets/bodies/bennu/shape/       Bennu OBJ
assets/rendering/assets/bodies/bennu/appearance/albedo/  Bennu JPEG
assets/models/centroid/     plain image-only centroid ONNX
assets/models/yolo/examples/model_configs/  YOLO manifest
assets/models/yolo/models/onnx/             YOLO weights
```

Copy the whole demo directory, including ignored `external/` and `assets/`:

```sh
rsync -a --exclude=.git --exclude=/build --exclude=/deps --exclude=/output \
  /home/peterc/devDir/rendering-to-perception-demos-open-night2026/ \
  /path/on/new-machine/rendering-to-perception-demos-open-night2026/
cd /path/on/new-machine/rendering-to-perception-demos-open-night2026
scripts/run_local_bundle.sh render \
  --scene sphere --mode both \
  --centroid-model "$PWD/assets/models/centroid/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx" \
  --spp 1 --max-frames 1 --headless
```

The launcher changes to the demo root so the tracked camera YAML resolves,
sets `RENDERING_DATA` to the copied Bennu root, and puts bundled shared
libraries ahead of the binaries' original absolute RUNPATH entries. It leaves
`CUDA_VISIBLE_DEVICES` unchanged; set it explicitly when selecting a GPU. Set
`DEMO_BINARY_DIR=build/portable` to run a rebuilt executable from the copied
packages. Use `scripts/run_local_bundle.sh camera` with the
same camera options as the ordinary executable. For a video with all three
processors:

```sh
scripts/run_local_bundle.sh camera --video /path/to/video.mp4 --mode both \
  --centroid-model "$PWD/assets/models/centroid/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx" \
  --yolo-model "$PWD/assets/models/yolo/examples/model_configs/yolov7_640x640.ptafmodel" \
  --max-frames 1 --headless
```

Replace `--video` with `--frames-dir /path/to/frames` for a folder, or use
`--camera-index 0` for a webcam. The latter needs a device on the destination.
The copied YOLO manifest still resolves `../../models/onnx/yolov7_640x640.onnx`.
Verify the copied payload from the demo root with
`sha256sum -c external/BUNDLE.sha256` before use; the manifest omits symlinks,
whose targets remain relative inside each copied install.
For textured Bennu, add:

```sh
--albedo-jpeg "$PWD/assets/rendering/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg"
```

This runtime copy needs a compatible Ubuntu 24.04 x86-64 host with an NVIDIA
driver and a CUDA GPU supported by the copied native libraries.
The OS loader, glibc, libstdc++, X11/GLFW/OpenGL, codecs, and the NVIDIA driver
remain host components. Headless use does not require a display. The renderer
queries the selected logical CUDA device and records its name and compute
capability in `run.json`; it does not infer a physical index. The copied
prefixes can be used to rebuild the demo,
but that still requires a C++20 compiler, CMake, Eigen 3.4, CUDA toolkit 12.9,
OptiX headers, and the host development packages. The bundle does not contain
the KLT/SLAM source worktrees required to rebuild those libraries from source.

To configure against the copied installed packages, set `CMAKE_PREFIX_PATH` to
`external/native;external/slam-primitives;external/onnxruntime`,
`OpenCV_DIR=external/opencv/lib/cmake/opencv4`, and
`OPTIX_ROOT=external/optix`. Use absolute paths for these CMake values. A
separate `external/onnxruntime` retains its installed CMake package and CUDA
provider, while its unneeded `onnx_test_runner` binary is omitted. `external/`
and `assets/` are ignored by Git and must be included explicitly when copying.

For a demo-only rebuild after transfer, use the copied packages and the host
CUDA toolkit. This does not rebuild Spectra-RT, KLT, or AutoForge:

```sh
demo_root=$PWD
cmake -S "$demo_root" -B "$demo_root/build/portable" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$demo_root/external/native;$demo_root/external/slam-primitives;$demo_root/external/onnxruntime" \
  -DOpenCV_DIR="$demo_root/external/opencv/lib/cmake/opencv4" \
  -DOPTIX_ROOT="$demo_root/external/optix" \
  -DDEMO_ENABLE_ML=ON -DDEMO_BUILD_TESTS=OFF
cmake --build "$demo_root/build/portable" -j 8
```

## Copy checklist

- [ ] Copy this demo repository with ignored `external/` and `assets/`, plus
      tracked config/camera_rgb_wfov and the Spectra-RT patch. Verify
      `external/BUNDLE.sha256` after transfer.
- [ ] On the destination, run the bundled sphere+centroid smoke, then a camera
      video/folder smoke, then textured Bennu and YOLO as needed.
- [ ] For a dependency source rebuild, use Spectra-RT branch
      feature/implement-factorized-radiometry-mode at fdf46db. The tracked patch
      in this demo repo is for the older f948bd6 revision only; do not apply it
      again to a checkout that already contains the change.
- [ ] For a dependency source rebuild, copy the exact working trees of
      pyramidal-klt-for-space-nav and
      slam-primitives, including uncommitted and untracked source files. Their
      current clean Git revisions alone do not describe the compiled API.
- [ ] For a rebuild, install a matching C++/CUDA toolchain and build the native
      dependencies in the order below. The copied package prefixes cover
      OpenCV, OptiX headers, Spectra-RT, KLT, AutoForge, and ONNX Runtime.
- [ ] For Bennu, include the bundled OBJ and, for texture, JPEG. The built-in
      sphere needs neither file.
- [ ] For centroiding or YOLO, include the bundled model files and ML libraries.
- [ ] For live preview, provide an X11/OpenGL display. A display is not needed
      for a headless run, although GLFW and OpenGL are still link dependencies.
- [ ] For webcam input, provide a camera device and a working OpenCV video
      backend. A video file or image folder can replace the webcam.
- [ ] Copy the ignored evidence MP4 or frame directory only if the recorded
      output itself is needed. The demo can regenerate it.

### Source and binary provenance

| Component | Source used here | Transfer rule |
| --- | --- | --- |
| Demo | main, 72575a2 before current portability edits | Copy the complete working folder, including ignored external/ and assets/. Git alone does not carry the payload. |
| Spectra-RT | feature/implement-factorized-radiometry-mode, fdf46db | The runtime bundle contains its install. For a source rebuild, use this commit. The tracked patch is only for the older f948bd6 revision. Keep unrelated local edits separate. |
| KLT | feature/space-tailored-extraction, c714e1f | The runtime bundle contains its install. For a source rebuild, copy the working tree with its 28 modified paths or export those changes separately. |
| SLAM primitives | feature/extend-visual-features-support, 5e54f81 | The runtime bundle contains its install. For a source rebuild, copy the working tree with its 42 status entries, including untracked strong-ID and camera-type headers. |
| AutoForge deploy | develop, 03bb25c | The runtime bundle contains its install. Source is needed only when rebuilding that library; the current local edit is documentation only. |
| ONNX Runtime | installed 1.23.0 | Needed for ML builds. This machine has CPU and CUDA providers in a separate installation. |
| OptiX SDK | header OPTIX_VERSION 90000 | Needed to build Spectra-RT. This machine used a separate OptiX 9 SDK checkout. |

Original-machine locations for the copy operation:

    SPECTRA_SRC=/home/peterc/devDir/rendering-sw/spectra-rt
    KLT_SRC=/home/peterc/devDir/SLAM-repos/pyramidal-klt-for-space-nav
    SLAM_SRC=/home/peterc/devDir/SLAM-repos/slam-primitives
    AUTOFORGE_SRC=/home/peterc/devDir/ML-repos/torchAutoForge-deploy
    ORT_PREFIX=/home/peterc/devDir/ML-repos/onnxruntime/install
    OPTIX_ROOT=/home/peterc/devDir/dev-tools/nvidia/optix-sdk
    OPENCV_DIR=/usr/local/lib/cmake/opencv4
    RENDERING_DATA=/media/peterc/SCRATCH_PRO/simulation_rendering_assets
    CENTROID_MODEL=/home/peterc/devDir/ML-repos/.worktrees/android-app-experiment/out/mobile/camera-model-input/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx
    YOLO_MANIFEST=/home/peterc/devDir/ML-repos/torchAutoForge-deploy/examples/model_configs/yolov7_640x640.ptafmodel
    YOLO_ONNX=/home/peterc/devDir/ML-repos/torchAutoForge-deploy/models/onnx/yolov7_640x640.onnx

The KLT checkout has uncommitted changes to its track-ID interface, and the
demo calls value() on each active strong ID. The SLAM checkout has corresponding
untracked strong-ID headers. A fresh clone of only c714e1f and 5e54f81 is
therefore not a verified replacement for those working trees. Preserve their
source state before moving machines. Their tracked-diff SHA-256 values in this
snapshot are 4f47f8e18f1a27cbd72ffdc853c358049d9de57fa43c8c7c84be3badae2c233f
and 9abb5bce9bb5e90270749b0248f641c7020565b8a275d686cbbf1afc2628916c,
respectively; the latter hash does not include untracked files.

The Spectra-RT patch file has SHA-256
1292775b604623200620b3f43f95165a629d518f2bc4b7b143dfe53701cfe4e3.
It applies to the clean f948bd6 index and matches the five files later
committed as fdf46db. The unrelated quick-demo script and wrapper/config edits
remain separate working-tree changes and must be preserved separately.
The local Spectra-RT checkout has MathCore_for_ComputerVision, OWL,
meshoptimizer, and wrap gitlinks. Reproduce the dependency submodule
revisions needed by its native build; Python wrapper generation is not part
of this demo build.

### External data and models

These paths identify the original inputs copied into `assets/`. Use the
`assets/` paths in the local-bundle commands above after transfer. Sizes and
hashes identify the exact inputs used here.

| Input | Required for | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| simulation_rendering_assets/assets/bodies/bennu/shape/Bennu_17M_withMtl_split2.obj | Bennu scene | 1,989,148,196 | 0a0f2b0536612a4afce59f4b8e4de7a7ff1229dcfa15847fc7775396e021d1f8 |
| simulation_rendering_assets/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg | Textured Bennu | 61,656,124 | 65011c994b5db391df7f8be501ed768f20d789f4905a00b622e30e1a39992be9 |
| best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx | Centroiding | 16,796,290 | 8ba4f46355b0f6b542ec848b4c1130760bea194f9bf3b16e7b36bad9f380af4b |
| examples/model_configs/yolov7_640x640.ptafmodel | YOLO manifest | 369 | 2280ac3bf1a5b8498a1663f04a471eeed4e09bca97042ac67ef33e8c3eefd7bc |
| models/onnx/yolov7_640x640.onnx | YOLO weights | 148,078,692 | 99b8030554ed03fa15be185acd58065df6c64c2a79af392a419c87af857aaf63 |

The YOLO manifest resolves its artifact as
../../models/onnx/yolov7_640x640.onnx relative to the manifest directory.
Preserve that relative layout or edit artifact_path in the copied manifest.
The centroid model is a standalone ONNX file. The default WFOV camera YAML and
its provenance JSON are tracked inside this demo repository; the PDF and slide
paths in provenance.json are historical source references, not runtime inputs.
The OBJ contains the shape and UVs; the demo overrides its material rather than
loading its MTL. The textured path converts the JPEG to scalar linear luminance.

The optional recorded output is build/evidence_bennu_phase_sweep_msac.mp4
(2,598,247 bytes, SHA-256
39659398fe8f130007bc04f26992a85b4b2b69e13aed8508dd90e24fdd48b5e8).
The accompanying frame directory is about 37 MiB and contains run.json,
frames.jsonl, and 50 annotated PNGs. Both paths are ignored by Git.

## Toolchain and host requirements

| Item | Verified local installation | Why it matters |
| --- | --- | --- |
| OS and compiler | Ubuntu 24.04.4 x86-64, GCC 13.3, C++20 | The native stack was built here. Other platforms are untested. |
| CMake and generator | CMake 3.28.3, Ninja 1.11.1 | Spectra-RT requires CMake 3.28 or newer with GCC 13. |
| CUDA and driver | CUDA toolkit 12.9, local driver 580.105.08 | Spectra-RT uses CUDA and OptiX. Runtime smokes passed on compute 8.9 and 12.0 GPUs. |
| GPU | Local: physical index 1, RTX 4070 Ti SUPER, compute 8.9; target: RTX 5070 Laptop GPU, compute 12.0 | The renderer queries the selected logical device. The copied native libraries must support its architecture. |
| OptiX | SDK 9 headers and compatible driver | Required by Spectra-RT; point OPTIX_ROOT to the SDK root. |
| OpenCV | CMake package 4.10.0 from /usr/local, Python cv2 4.10.0-dev | The demo requests OpenCV 4.10. The system pkg-config default here reports 4.6.0, so set OpenCV_DIR explicitly. |
| Eigen | 3.4.0 | Required by KLT, SLAM primitives, and AutoForge. |
| Preview libraries | GLFW 3.3.10 and OpenGL | CMake links these even for headless executables. |
| Test Python | Python 3.12.3, NumPy 1.26.4, cv2 4.10.0-dev | Needed by camera_stream_contract, not by the C++ executables. |
| Video encoding | FFmpeg 6.1.1 | Optional; converts saved PNGs to MP4. |
| ML runtime | ONNX Runtime 1.23.0 | Required only with DEMO_ENABLE_ML=ON. CUDA inference also needs its CUDA provider, CUDA libraries, and cuDNN 9. |

The original ML-enabled executable has RUNPATH entries for absolute paths on
this machine. `run_local_bundle.sh` supplies `LD_LIBRARY_PATH` ahead of those
entries, so copied binaries resolve the bundled libraries first. Directly
executing `external/bin/*` without the launcher can fall back to unavailable
original paths. The copied ONNX Runtime headers and libraries occupy about
2.5 GiB; its 781 MiB test runner is omitted. The Bennu OBJ is nearly 2 GiB on
disk and takes additional memory while loading. No minimum host RAM measurement
was made.

## Rebuild on a matching machine

Run commands from the demo repository root. The example below places new
install prefixes under its ignored deps/ directory. Replace every /path/to
value. A copied KLT and SLAM working tree must include their dirty source
state described above. This sequence is adapted from the successful local
build; the complete transfer has not been exercised on a second machine.

    cd /path/to/rendering-to-perception-demos-open-night2026
    DEMO="$PWD"
    SLAM_SRC=/path/to/slam-primitives
    KLT_SRC=/path/to/pyramidal-klt-for-space-nav
    SPECTRA_SRC=/path/to/spectra-rt
    AUTOFORGE_SRC=/path/to/torchAutoForge-deploy
    ORT_PREFIX=/path/to/onnxruntime/install
    OPTIX_ROOT=/path/to/optix-sdk
    OPENCV_DIR=/path/to/opencv4/lib/cmake/opencv4
    PREFIX="$DEMO/deps"
    SLAM_PREFIX="$DEMO/deps/slam"

Start with SLAM primitives, then Spectra-RT and KLT. The SLAM configure line
below is a portable adaptation of its installed package setup; it was not
separately run as a fresh configure in this handoff. Check that the installed
package exposes slam-primitives 0.2.0 before building KLT.

    cmake -S "$SLAM_SRC" -B "$DEMO/build/slam" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SLAM_PREFIX" \
      -DENABLE_TESTS=OFF "-Dslam-primitives_BUILD_EXAMPLES=OFF" \
      "-Dslam-primitives_BUILD_PROGRAMS=OFF" -DOpenCV_DIR="$OPENCV_DIR"
    cmake --build "$DEMO/build/slam" -j 8
    cmake --install "$DEMO/build/slam"

For a clean Spectra-RT f948bd6 checkout, first apply the tracked patch once.
Skip the apply step when the copied worktree already has it. The patch adds
one-channel texture admission and focused tests; it does not change shader
transport math.

    git -C "$SPECTRA_SRC" apply --check \
      "$DEMO/patches/0001-admit-scalar-albedo-in-factorized-transport.patch"
    git -C "$SPECTRA_SRC" apply \
      "$DEMO/patches/0001-admit-scalar-albedo-in-factorized-transport.patch"
    cmake -S "$SPECTRA_SRC" -B "$DEMO/build/spectra" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_CUDA_ARCHITECTURES=89 -DOPTIX_ROOT="$OPTIX_ROOT" \
      -DOpenCV_DIR="$OPENCV_DIR" -DENABLE_OPENGL=OFF \
      -DENABLE_TESTS=OFF -Dspectra_rt_BUILD_EXAMPLES=OFF \
      -Dspectra_rt_BUILD_PROGRAMS=OFF
    cmake --build "$DEMO/build/spectra" -j 8
    cmake --install "$DEMO/build/spectra"

    cmake -S "$KLT_SRC" -B "$DEMO/build/klt" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_PREFIX_PATH="$SLAM_PREFIX" -DOpenCV_DIR="$OPENCV_DIR" \
      -DENABLE_TESTS=OFF -Dpyramidal_klt_BUILD_PIPELINE=ON \
      -Dpyramidal_klt_BUILD_DEMOS=OFF \
      -Dpyramidal_klt_BUILD_EXAMPLES=OFF \
      -Dpyramidal_klt_BUILD_PROGRAMS=OFF
    cmake --build "$DEMO/build/klt" -j 8
    cmake --install "$DEMO/build/klt"

For KLT-only operation, omit AutoForge and ONNX Runtime and configure the demo
with DEMO_ENABLE_ML=OFF. This still builds the render executable and still
requires Spectra-RT, CUDA/OptiX, GLFW, and OpenGL. For centroiding or YOLO,
install AutoForge first:

    cmake -S "$AUTOFORGE_SRC" -B "$DEMO/build/autoforge" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -Donnxruntime_DIR="$ORT_PREFIX/lib/cmake/onnxruntime" \
      -DOpenCV_DIR="$OPENCV_DIR" -DENABLE_TESTS=OFF \
      -Dautoforge_deploy_BUILD_EXAMPLES=OFF \
      -Dautoforge_deploy_BUILD_PROGRAMS=OFF \
      -Dautoforge_deploy_ENABLE_IMAGE_SUPPORT=OFF
    cmake --build "$DEMO/build/autoforge" -j 8
    cmake --install "$DEMO/build/autoforge"

    cmake -S "$DEMO" -B "$DEMO/build/demo" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PREFIX;$SLAM_PREFIX;$ORT_PREFIX" \
      -DOpenCV_DIR="$OPENCV_DIR" -DOPTIX_ROOT="$OPTIX_ROOT" \
      -DDEMO_ENABLE_ML=ON
    cmake --build "$DEMO/build/demo" -j 8
    ctest --test-dir "$DEMO/build/demo" --output-on-failure

Change DEMO_ENABLE_ML to OFF and omit ORT_PREFIX from CMAKE_PREFIX_PATH for
the KLT-only variant. If Python 3.12, NumPy, or cv2 is unavailable, set
DEMO_BUILD_TESTS=OFF at configure time and record that the camera behavior test
was skipped. Keep one OpenCV 4.10 installation throughout the C++ builds.

## Run modes and inputs

All commands below assume the current GPU layout. Run from the demo root so
the default relative config/camera_rgb_wfov/camera.yaml resolves. Alternatively
pass an absolute --camera-yaml path. The rendered sensor is fixed to 2048 x
1536 Bayer raw with a centered, zero-skew pinhole camera; the bundled YAML is
the measured-run input. Rendered KLT refuses an incompatible calibration.

    CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo \
      --scene sphere --spp 8 --max-frames 3 --headless \
      --output-dir build/sphere_smoke

    export RENDERING_DATA=/path/to/simulation_rendering_assets
    CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo \
      --scene bennu --mode klt --spp 8 --max-frames 3 \
      --orbit-step-deg 0.1 --headless --output-dir build/bennu_klt

    CENTROID_MODEL=/path/to/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx
    CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo \
      --scene bennu --mode both --centroid-model "$CENTROID_MODEL" \
      --spp 8 --max-frames 3 --headless --output-dir build/bennu_both

    CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo \
      --scene bennu --mode both --centroid-model "$CENTROID_MODEL" \
      --albedo-jpeg "$RENDERING_DATA/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg" \
      --spp 8 --max-frames 3 --headless --output-dir build/bennu_textured_both

RENDERING_DATA supplies the default OBJ beneath assets/bodies/bennu/shape.
Pass --model /path/to/Bennu_17M_withMtl_split2.obj instead if only that file
was copied. The JPEG is optional. The default scene is sphere and the default
mode is KLT. Centroid and both modes require an ML-enabled build and a
--centroid-model path. The fixed 6300-electron display scale comes from the
bundled sensor profile; no per-frame normalization is used.

The camera program accepts exactly one of --camera-index, --video, or
--frames-dir. A file source is paced at --fps, default 15. Frame files are
sorted naturally and must retain one resolution. The physical webcam path
was drafted and built but not tested with a device on this machine. For
ordinary Earth-scene footage, --klt-extraction generic disables the
illuminated-body mask; space is the default.

    build/demo/camera_stream_demo --frames-dir /path/to/frames \
      --mode klt --headless --output-dir build/folder_klt
    build/demo/camera_stream_demo --video /path/to/video.mp4 \
      --mode both --centroid-model "$CENTROID_MODEL" \
      --headless --output-dir build/video_both
    build/demo/camera_stream_demo --camera-index 0 \
      --mode centroid --centroid-model "$CENTROID_MODEL"
    CUDA_VISIBLE_DEVICES=1 build/demo/camera_stream_demo \
      --video /path/to/video.mp4 --mode both \
      --centroid-model "$CENTROID_MODEL" \
      --yolo-model /path/to/examples/model_configs/yolov7_640x640.ptafmodel \
      --headless --output-dir build/video_all

The camera executable accepts the device visibility selected by the caller.
The YOLO manifest asks ONNX Runtime for CUDA then CPU and selects logical device
zero among visible devices. Centroiding requests the CPU provider. File and
webcam KLT have no calibration, so MSAC status remains OFF.

With no --headless flag, GLFW opens the preview. Left-drag orbits the rendered
camera, right-drag pans, wheel changes distance, arrows pan in the camera
plane, R resets the selected view, and Esc exits. The camera preview uses Esc.
Injected Xvfb events exercised the render controls; a physical display was
not available for testing. A headless run still needs --max-frames for the
renderer. Saved frame PNGs are annotated; MP4 encoding is a separate FFmpeg
step, not an online frame-rate measurement.

## Implementation map and data flow

| File | Owner and main contract |
| --- | --- |
| [CMakeLists.txt](../../CMakeLists.txt) | Always link Spectra-RT, KLT, OpenCV, GLFW, and OpenGL. Gate AutoForge/ONNX code with DEMO_ENABLE_ML; gate the Python behavior test with DEMO_BUILD_TESTS. |
| [render_stream_demo.cpp](../../src/render_stream_demo.cpp) | Parse render options, create sphere/Bennu and fixed Sun, configure factorized physical sensor, update camera/body, read Bayer electrons, reconstruct fixed-scale grayscale, pass the calibrated frame to the shared processor. Query the selected CUDA device and write run metadata. |
| [camera_stream_demo.cpp](../../src/camera_stream_demo.cpp) | Capture webcam/video/folder frames, pace files, retain source indices, drain at EOF, use one-slot capture and preview mailboxes, and pass unknown calibration explicitly. |
| [demo_core.h](../../src/demo_core.h) and [demo_core.cpp](../../src/demo_core.cpp) | Own the frontend KLT pipeline, optional model adapter, eight-position display trails, per-frame summary, JSONL/PNG writer, and GLFW preview. This is the shared frame ownership boundary. |
| [model_adapter.cpp](../../src/model_adapter.cpp) | Bind centroid ONNX to CPU and YOLO manifest to its declared backend; normalize inputs, decode the 1x2 centroid, decode YOLO raw rows, apply score filtering and class-aware NMS. |
| [stream_contract.py](../../tests/stream_contract.py) | Generate disposable folder frames and assert ordering, IDs, mask recovery, dimension rejection, and MSAC OFF for uncalibrated media. |
| [camera.yaml](../../config/camera_rgb_wfov/camera.yaml) and [provenance.json](../../config/camera_rgb_wfov/provenance.json) | Keep the provisional WFOV sensor response and its source limitations in this repository. |
| [scalar-texture patch](../../patches/0001-admit-scalar-albedo-in-factorized-transport.patch) | Change Spectra-RT factorized material admission and focused GPU tests only. The renderer source remains owned by Spectra-RT. |

The render target defines EIGEN_MALLOC_ALREADY_ALIGNED=0 to match Spectra-RT's
native-tuned Eigen allocation ABI. Spectra-RT and KLT also export a logger
header with the same relative name; the render translation unit includes both
named headers before shared APIs. Preserve these build details when changing
include order, compiler flags, or packaging.

The render worker uses Spectra-RT factorized SENSOR_MEASUREMENT, a finite
area 5778 K Sun at 1 AU, worldUnit_m=1000, stratified ray sampling, four
direct samples, and no irradiance bypass. It reads raw expected electrons
without modifying them, reconstructs Bayer grayscale, then scales that
grayscale once using the configured full-well reference for KLT and
centroiding. The Sun stays fixed in world coordinates. Bennu spins about
model +Z with a 4.296007-hour reference period; --spin-multiplier multiplies
elapsed wall time. The camera orbit step is per processed render frame.
This is a controlled demonstration, not a dated attitude ephemeris.

The shared processor uses CFrontendKltPipeline with illuminated-body
masking and Kmeans coverage. It does not pass renderer depth, hit IDs, or
truth masks into KLT. The frontend owns track IDs, feature replenishment,
mask retries, and MSAC retirement. For rendered KLT, calibration comes
from the same centered pinhole sensor geometry used to trace rays:
fx=fy=5840.90918 px, cx=1024 px, cy=768 px, and a 1 px MSAC tolerance.
The demo keeps at most eight positions for drawing each surviving ID, from
red through orange to yellow. The cache is not a second tracker.

Centroid and KLT consume the same grayscale frame in both mode. The
centroid model outputs normalized [1,2] coordinates; the adapter maps
them to source pixels without clamping and reports OK, OUTSIDE, or ERROR.
YOLO consumes the original BGR frame (or a gray-to-BGR copy), reports up
to 50 boxes after score 0.25 and class-aware IoU 0.45 suppression, and
draws green boxes. Model inference accuracy was not evaluated here.

The renderer has one worker and GLFW stays on the main thread. The
camera program has separate capture and processing workers, plus the
main preview thread. One-slot mailboxes bound backlog. A published
preview contains results from a single source frame; dropped source and
processed indices may differ when work falls behind. Console text,
overlay, and frames.jsonl come from one SFrameSummary. With
--output-dir, CFrameWriter creates run.json, frames.jsonl, and
frames/frame_000000.png-style annotated images. Without that flag,
interactive runs do not write output. Rendered run.json uses schema version 2
because `cuda_device` replaces the former `physical_gpu_index` field; camera
run.json remains version 1. Rendered metadata records camera pixels, MSAC
threshold, selected CUDA device facts, scene, and spin settings. Each
frames.jsonl row carries source_index and
processed_index, active_track_ids, KLT/mask/MSAC and centroid states, stage
times, and optional phase/spin values. Read these structured fields instead
of parsing the overlay text.

## Physical and geometric invariants

- Keep factorized transport achromatic. The Spectra-RT patch accepts only
  valid one-channel scalar textures on achromatic Lambertian or
  Lommel-Seeliger materials; RGB/RGBA and chromatic coefficients remain
  rejected. The CUDA upload replicates the scalar into RGB without another
  sRGB transform. A 128/255 map scaled raw factorized output and each of
  three sensor bands by 128/255 in the GPU test. The 0.05 Bennu coefficient
  and JPEG are demonstration inputs, not measured reflectance.
- Keep the physical measurement separate from display scaling. Do not
  normalize each frame independently or feed rendered truth labels into
  KLT. The provisional optical response does not model noise, ADC,
  saturation, distortion, or a calibrated lens.
- Keep rendered KLT intrinsics matched to the traced rays. Non-pinhole,
  skewed, off-center, non-finite, or non-positive focal calibration is
  rejected before rendering. For uncalibrated camera media, keep MSAC OFF
  until a matching camera contract is provided.
- Treat MSAC output as model-based rejection, not ground-truth labeling.
  WAIT and FAIL perform no geometric rejection. A rotating body and
  changing illumination can violate static-scene assumptions; inspect
  surviving trails and tracking counts before interpreting them.
- Preserve Spectra-RT benchmark output layout. This demo writes only
  under its explicit --output-dir and does not move or rename renderer
  benchmark products.

## Verification and evidence available here

- The ignored bundle measures 4.9 GiB under `external/` and 2.1 GiB under
  `assets/`. `external/BUNDLE.sha256` covers 963 regular files. The five
  source asset/model hashes above match the copied files.
- A second-path copy under `/tmp/perception-bundle.cMwDEk` ran sphere KLT and
  centroiding (150 active IDs, centroid OK), camera video KLT, centroiding,
  and YOLO together (CPU centroid, CUDA YOLO, 150 active IDs), and textured
  Bennu KLT and centroiding (150 active IDs, centroid OK). All three used
  physical GPU 1. This was path relocation on the same host, not a test on a
  second machine.
- On `peterc-alien16x`, `sha256sum -c --quiet external/BUNDLE.sha256` passed.
  The transferred source rebuilt in `build/portable` with the
  target's CUDA 12.9 toolkit. `DEMO_BINARY_DIR=build/portable` ran one headless
  sphere frame through the launcher on its RTX 5070 Laptop GPU. `run.json`
  reports CUDA logical device 0 and compute 12.0; KLT reported 150 active IDs.
  A frame-folder camera smoke using that annotated PNG reported 149 KLT IDs,
  centroid `OK`, and one YOLO box. The model diagnostics reported CPU centroid
  and CUDA/CPU YOLO providers. The annotated input does not measure detection
  accuracy. A physical webcam was not available for this test.
- The two ignored `external/bin` executables were refreshed from the local
  build, and only their two rows changed in `external/BUNDLE.sha256`. After
  transfer, the target checksum passed again. The default launcher then ran
  one sphere frame and one camera frame-folder KLT+centroid+YOLO frame on the
  RTX 5070 Laptop GPU without `DEMO_BINARY_DIR` or `CUDA_VISIBLE_DEVICES`.
- The target imported the reworded commit history through a Git bundle and
  moved only its local `main`. Its tracked working-tree diff hash remained
  unchanged. All ten SSH signatures verified there with a temporary copy of
  the source machine's allowed-signers file; no permanent Git config changed.
- CMake configured and rebuilt both demos from the second path against the
  copied installed packages. Its first configure exposed a missing OpenCV
  `lib/opencv4/3rdparty` directory; the packager now includes it. With the
  launcher library path, `ldd` resolves Spectra-RT, KLT, AutoForge, ONNX
  Runtime, OpenCV, CUDA runtime, cuBLAS, cuFFT, and cuDNN under that second
  path. The NVIDIA driver, GLFW, OpenGL, and system codecs resolve from the
  host.

- The ML and no-ML demo variants built with GCC 13.3; camera_stream_contract
  passed with Python 3.12, OpenCV Python, and NumPy.
- On physical GPU 1, Spectra-RT testFactorizedTransport passed 1,379
  assertions in 26 cases, and testSceneMaterials passed 386 assertions in
  19 cases. The focused scalar-texture case passed 17 assertions.
- Textured Bennu completed in klt, centroid, and both modes. The
  three-frame combined run retained 150 IDs and reported centroid OK in
  every frame.
- The 50-frame untextured Bennu phase sweep ran KLT and centroiding
  together. Phase went from 11.976712 to 145.126152 degrees. MSAC was
  VALID on 49 transitions and retired 1,453 correspondences across the
  stream; 30 tracks remained at the final crescent. Centroid was OK on
  all 50 frames and no frames were dropped.
- The H.264 evidence MP4 is 2048 x 1536, 50 frames, 8.33 seconds at an
  intentionally chosen 6 fps playback rate. This is recorded output,
  not measured interactive throughput.
- Xvfb input injection exercised left/right drag, wheel, arrows, reset,
  and exit. Neither a physical webcam nor a physical interactive display
  was tested on this machine.

On a destination machine, first run the three-frame sphere KLT headless
smoke, then the camera folder contract, then one Bennu frame, then the
textured and model paths for which assets are present. Compare run.json
camera fields, physical GPU index, MSAC status, and source/processed IDs
before trying a long sweep. The source test and runtime evidence above
establish this machine's behavior; they are not a destination-machine
validation.

## Where an agent should change things

1. Inspect git branch, HEAD, index, unstaged changes, untracked files,
   and worktrees in the demo and every external checkout before editing.
   The KLT and SLAM working trees are intentionally dirty; preserve
   their uncommitted source. Stage only reviewed demo paths and do not
   push without a new user instruction.
2. For a new GPU, select visibility with `CUDA_VISIBLE_DEVICES` if needed and
   run a bounded smoke through `scripts/run_local_bundle.sh`. The renderer's
   `cuda_device` metadata reports the selected logical device, name, and compute
   capability. If the copied Spectra-RT build cannot run on that GPU, rebuild
   Spectra-RT for its CUDA architecture before repeating the smoke.
3. For another camera, make the traced-ray model and KLT intrinsics
   agree before enabling MSAC. Change camera YAML parsing/validation in
   the owning renderer if needed, and test off-center or distortion
   behavior explicitly. Do not silently reuse the current centered
   WFOV intrinsics.
4. For source integration or renderer material behavior, keep code in
   its owning repository. The scalar-texture admission change is now commit
   `fdf46db` on Spectra-RT's factorized-radiometry branch. A further
   external-source edit is a review gate under this demo's
   [AGENTS.md](../../AGENTS.md) and [PLAN.md](../../PLAN.md).
5. For stream threading, frame identity, and output format, edit the
   shared processor only when both programs need the policy. Preserve
   one summary record per processed frame and the one-slot backlog
   bound. Add tests for externally visible behavior, not source-text
   matching.
6. After a change, run clang-format on changed C++, build the relevant
   ML/no-ML variant, run CTest or a focused GPU 1 smoke, inspect JSON
   and annotated output, and review the full diff. If a commit is
   requested, follow the imperative sentence-case style in PLAN.md,
   inspect the complete staged index, and keep external work excluded.

The installed KLT/SLAM dependency state is copied into `external/`, but its
uncommitted source worktrees are not. Rebuild those libraries from source only
after securing their reviewed source snapshots.
