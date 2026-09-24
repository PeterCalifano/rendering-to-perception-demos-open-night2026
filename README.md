# Rendering-to-perception demos

Two C++20 programs use the same frame processor. `render_stream_demo` renders a Bayer sensor frame with Spectra-RT, reconstructs fixed-scale grayscale, and runs space-aware KLT, centroiding, or both. `camera_stream_demo` accepts a webcam, video, or image folder and can also run YOLOv7. Each processed frame gets colored overlays, a compact preview summary, one console line, and optional JSONL and PNG output. The stage record and measured checks are in [PLAN.md](PLAN.md).

This repo retains only the MIT license from `cpp_cuda_template_project` commit `f207d2a`; it contains no ROS overlay, wrapper, CUDA placeholder, or template-conformance suite.

## Build

These commands use the checkouts and packages on this machine. Dependencies install into this repo's ignored `deps/` directory. Builds use OpenCV 4.10 from `/usr/local`.

```sh
cd /home/peterc/devDir/rendering-to-perception-demos-open-night2026
DEVDIR=/home/peterc/devDir
PREFIX="$PWD/deps"
SLAM_PREFIX="$DEVDIR/SLAM-repos/slam-primitives/install"
ORT_PREFIX="$DEVDIR/ML-repos/onnxruntime/install"
OPTIX_ROOT="$DEVDIR/dev-tools/nvidia/optix-sdk"

cmake -S "$DEVDIR/rendering-sw/spectra-rt" -B build/spectra -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_CUDA_ARCHITECTURES=89 -DOPTIX_ROOT="$OPTIX_ROOT" \
  -DOpenCV_DIR=/usr/local/lib/cmake/opencv4 -DENABLE_OPENGL=OFF \
  -DENABLE_TESTS=OFF -Dspectra_rt_BUILD_EXAMPLES=OFF \
  -Dspectra_rt_BUILD_PROGRAMS=OFF
cmake --build build/spectra -j 8
cmake --install build/spectra

cmake -S "$DEVDIR/SLAM-repos/pyramidal-klt-for-space-nav" -B build/klt -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$SLAM_PREFIX" -DOpenCV_DIR=/usr/local/lib/cmake/opencv4 \
  -DENABLE_TESTS=OFF -Dpyramidal_klt_BUILD_DEMOS=OFF \
  -Dpyramidal_klt_BUILD_EXAMPLES=OFF -Dpyramidal_klt_BUILD_PROGRAMS=OFF
cmake --build build/klt -j 8
cmake --install build/klt

cmake -S "$DEVDIR/ML-repos/torchAutoForge-deploy" -B build/autoforge -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -Donnxruntime_DIR="$ORT_PREFIX/lib/cmake/onnxruntime" \
  -DOpenCV_DIR=/usr/local/lib/cmake/opencv4 -DENABLE_TESTS=OFF \
  -Dautoforge_deploy_BUILD_EXAMPLES=OFF -Dautoforge_deploy_BUILD_PROGRAMS=OFF \
  -Dautoforge_deploy_ENABLE_IMAGE_SUPPORT=OFF
cmake --build build/autoforge -j 8
cmake --install build/autoforge

cmake -S . -B build/demo -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PREFIX;$SLAM_PREFIX;$ORT_PREFIX" \
  -DOpenCV_DIR=/usr/local/lib/cmake/opencv4 -DOPTIX_ROOT="$OPTIX_ROOT" \
  -DDEMO_ENABLE_ML=ON
cmake --build build/demo -j 8
ctest --test-dir build/demo --output-on-failure
```

`DEMO_ENABLE_ML=OFF` builds KLT modes without AutoForge. The camera behavior test needs Python 3.12, OpenCV Python, and NumPy; set `DEMO_BUILD_TESTS=OFF` if those test dependencies are absent. The render target defines `EIGEN_MALLOC_ALREADY_ALIGNED=0` to match Spectra-RT's native-tuned Eigen allocation ABI. Spectra-RT and KLT both export `utils/logging/CLogger.h`; the render translation unit includes both named headers before shared APIs.

## Rendered stream

```sh
CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo --scene sphere --spp 8

CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo --scene sphere \
  --mode both --centroid-model /path/to/centroid.onnx \
  --orbit-step-deg 0.1 --spp 8 --max-frames 3 --headless \
  --output-dir build/render_both

export RENDERING_DATA=/media/peterc/SCRATCH_PRO/simulation_rendering_assets
CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo --scene bennu \
  --view whole_body --spp 8 --max-frames 3 --orbit-step-deg 0.1 \
  --headless --output-dir build/bennu_untextured

# Run a separate higher-sample reference; do not treat its cost as preview FPS.
CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo --scene sphere \
  --spp 32 --max-frames 1 --headless --output-dir build/sphere_reference_32spp
```

The default rendered mode is `klt`; `centroid` and `both` require `--centroid-model` and an ML build. The default scene is an illuminated sphere for quick checks. Bennu loads its full 17-million-triangle OBJ and UVs from `RENDERING_DATA`; `--model` can supply another OBJ. View presets are `whole_body`, `approach`, and `surface`.

Bennu rotates about its model `+Z` axis using elapsed wall time and the [NASA PDS nominal sidereal period of 4.29746 hours](https://pds.nasa.gov/ds-view/pds/viewProfile.jsp?dsid=EAR-A-I0037-5-BENNUSHAPE-V1.0). `--spin-multiplier 1` is the default physical rate; `0` freezes the body, and values above 1 accelerate it. The multiplier changes body orientation only. The sphere fixture stays static. This is a nominal spin about the model pole, without a dated attitude or orbital ephemeris. At 1x, rotation is barely visible in a short run.

The finite Sun stays fixed in world coordinates at 1 AU along `[1,1,0.3]`. Camera movement changes the Sun–body–camera phase angle; body spin changes which facets face the light. Use `--camera-azimuth-deg` to set the initial camera angle and `--orbit-step-deg` for a reproducible camera step per rendered frame. Dragging and arrow controls remain available during interactive preview. `R` restores the selected view and initial azimuth. `run.json` records the Sun motion, spin period and multiplier, and camera commands. Each rendered frame reports its phase angle, body rotation phase, and scene-instance update time in the overlay and JSONL.

To reproduce a short trail video, save ten Bennu frames with `--orbit-step-deg 0.25 --spp 8 --max-frames 10 --headless --output-dir build/evidence_bennu_trails`, then encode them:

```sh
ffmpeg -framerate 4 -i build/evidence_bennu_trails/frames/frame_%06d.png \
  -c:v libx264 -crf 18 -pix_fmt yuv420p build/evidence_bennu_trails.mp4
```

To move from a near-full to a crescent view at a readable pace, start near the fixed Sun azimuth (45 degrees), then step the camera 3 degrees per rendered frame. The example accelerates Bennu's spin 250x so it is visible during a short recording. The model path below was used on this machine; replace it if that external worktree moves. Omit `--mode both --centroid-model "$CENTROID_MODEL"` for KLT only.

```sh
CENTROID_MODEL=/home/peterc/devDir/ML-repos/.worktrees/android-app-experiment/out/mobile/camera-model-input/best_model_plain_traveling-goat-68_22b61bbd4ddd.onnx
CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo --scene bennu --view whole_body \
  --camera-azimuth-deg 45 --orbit-step-deg 3 --spin-multiplier 250 \
  --mode both --centroid-model "$CENTROID_MODEL" --spp 8 --max-frames 50 \
  --headless --output-dir build/evidence_bennu_phase_sweep
ffmpeg -framerate 6 -i build/evidence_bennu_phase_sweep/frames/frame_%06d.png \
  -c:v libx264 -crf 18 -pix_fmt yuv420p build/evidence_bennu_phase_sweep.mp4
```

The MP4 playback rate is an encoding choice, not a measured online frame rate. The frame summaries and PNGs come directly from the running renderer and perception pipeline.

The overlay uses up to eight recent positions per surviving KLT ID. Old positions are red, the middle is orange, and the current point is yellow. This bounded cache is for drawing only; KLT owns the IDs and track lifetime.

The optional `--albedo-jpeg FILE` path decodes a grayscale or BGR JPEG, converts sRGB to linear luminance, quantizes one channel, and binds it as Lambertian albedo. **It currently reaches Spectra-RT's factorized texture-admission error.** [The review patch](patches/0001-admit-scalar-albedo-in-factorized-transport.patch) targets Spectra-RT `f948bd6` and passes `git apply --check`; it has not been applied or GPU-tested. After approval to edit that checkout, rebuild and install Spectra-RT before using:

```sh
CUDA_VISIBLE_DEVICES=1 build/demo/render_stream_demo --scene bennu \
  --albedo-jpeg "$RENDERING_DATA/assets/bodies/bennu/appearance/albedo/Bennu_OSIRIS-REx_5cm_v1.jpg" \
  --spp 8 --max-frames 1 --headless --output-dir build/bennu_textured
```

The bundled [WFOV response](config/camera_rgb_wfov/provenance.json) is provisional: 2048 x 1536 GRBG, 12.85 mm, f/2.8, 1 ms, and 440–1000 nm. The renderer uses a finite-area 5778 K Sun at 1 AU, physical sensor measurement, four direct samples, stratified sampling, and `worldUnit_m=1000`. KLT and centroiding see one reconstructed grayscale frame. Its 8-bit value uses the fixed 6300-electron full-well reference throughout a run; it is not normalized per frame. Noise, ADC, saturation, distortion, and calibrated optics are not modeled.

## Webcam, video, and image folders

Choose exactly one source. Folder images are naturally sorted and must retain one resolution. File sources pace capture at `--fps` (default 15). Capture and processing each use a one-slot mailbox, so source indices can skip when processing falls behind. EOF drains the pending frame.

```sh
build/demo/camera_stream_demo --frames-dir /path/to/frames --fps 5
build/demo/camera_stream_demo --video /path/to/clip.avi --mode both \
  --centroid-model /path/to/centroid.onnx --output-dir build/video_both
build/demo/camera_stream_demo --camera-index 0 --mode centroid \
  --centroid-model /path/to/centroid.onnx

CUDA_VISIBLE_DEVICES=1 build/demo/camera_stream_demo --video /path/to/clip.avi \
  --mode both --centroid-model /path/to/centroid.onnx \
  --yolo-model /home/peterc/devDir/ML-repos/torchAutoForge-deploy/examples/model_configs/yolov7_640x640.ptafmodel
```

KLT uses illuminated-body masking and Kmeans coverage by default. Space-aware extraction needs a resolved, lit body with enough image contrast; a dark or tiny target can yield `EMPTY` and zero new points until later frames. For ordinary footage, pass `--klt-extraction generic` to disable the illuminated-body mask. Centroiding runs on CPU. The current YOLOv7 manifest reports CUDA then CPU providers under `CUDA_VISIBLE_DEVICES=1`; output is filtered at score 0.25 and suppressed per class at IoU 0.45. Video and image folders were tested here. A physical webcam was unavailable.

## Preview and output

The render preview accepts left-drag to orbit, right-drag to pan, wheel to change distance, arrow keys to pan in the camera plane, `R` to restore the selected view and launch azimuth, and `Esc` to exit. The camera preview uses `Esc`. Resizing preserves image aspect ratio and the detector resolution remains fixed. Headless rendering requires `--max-frames`.

The first summary line shows source and processed indices, timestamp, KLT active/tracked/new/lost counts, centroid status and pixel coordinate, and YOLO count when enabled. Rendered frames also show the Sun–body–camera phase angle and body rotation phase. The second line shows mask state (`OFF`, `WAIT`, `READY`, or `EMPTY`), eligible fraction, extraction retry, stage times, and cumulative dropped frames. Small camera frames use three lines. `EMPTY` is normal for a dark frame; retry continues until the illuminated body returns. The same record goes to `frames.jsonl`, including active track IDs. `--output-dir` also writes `run.json` and one annotated PNG per processed frame under `frames/`. Interactive runs write nothing unless this option is supplied.

Rendered JSONL records separate instance update, render, sensor readback, reconstruction and 8-bit scaling, KLT, and centroid times. The interactive programs print mean preview upload/draw/swap time after the window closes. These are stage timings, not a guaranteed display frame rate.

Keep demo output under the directory you choose. Spectra-RT's benchmark corpus and output tree have a separate required layout.
