# Rendering-to-perception demos

## Goal and acceptance

Build two C++20 streaming programs in this repo:

| Program | Source | Modes |
| --- | --- | --- |
| `render_stream_demo` | Spectra-RT factorized sensor frames | `klt`, `centroid`, `both` |
| `camera_stream_demo` | Webcam, video, or image folder | `klt`, `centroid`, `both`, with optional YOLOv7 |

`both` processes one source frame with KLT and centroiding and publishes one combined preview. The required first milestone is physical rendered frames with space-aware KLT tracks that retain IDs across camera motion. Centroiding and YOLOv7 are attempted with available models; record a concrete runtime blocker rather than delaying the KLT programs. No physical webcam is available here, so video and folder inputs supply acceptance evidence.

Before editing source, write this plan, start the goal, and record the current state of every external checkout. Update checkboxes, commands, results, review findings, deferrals, and commit hashes as work advances. A checked item requires evidence. Do not change any external repo source without stopping for user review.

## Technical contract

### Physical rendered stream

- Parse bundled provisional `camera_rgb_wfov` YAML and provenance. Use Spectra-RT sensor-aware `setLaunchParams`, `getSensorRaw`, and `ReconstructBayerImage`; keep raw expected electrons separate from display and algorithm input.
- Keep the 2048 x 1536 GRBG detector fixed while the GLFW viewport resizes. The profile uses a 12.85 mm lens, f/2.8, 1 ms exposure, and 440-1000 nm response. Do not claim calibrated optics, noise, ADC, distortion, or saturation.
- Load Bennu's 17-million-triangle OBJ and UVs from `RENDERING_DATA`, in kilometres with `world_unit_m=1000`. Provide whole-body, approach, and surface view presets; default to `[3,0,0]` km toward the origin.
- Use a finite-area Sun at 1 AU, radius 695700 km, direction `[1,1,0.3]`, 5778 K blackbody, photon-spectrum integration, stratified sampling, four direct samples, depth three, and no irradiance bypass. Default to eight samples/pixel for preview and document a 32-sample reference run.
- Keep the Sun fixed in world coordinates. Spin Bennu about its model `+Z` pole at the 4.296007-hour sidereal period measured during the 2018 OSIRIS-REx approach, using elapsed wall time and a nonnegative launch multiplier (`0` freezes it). Record the period, multiplier, and per-frame body phase. This is a demonstration rotation at a fixed reference rate, not an epoch-specific attitude ephemeris. Allow a launch azimuth so a camera sweep can start near the Sun direction; report the Sun-body-camera phase angle per frame.
- Load OBJ without its MTL. Convert the Bennu JPEG from sRGB to one-channel linear luminance and bind it as scalar Lambertian albedo after the external Spectra-RT admission patch. The 0.05 material coefficient and map are demonstration inputs, not calibrated reflectance.
- Use one declared electron-to-processing scale throughout a run. Never normalize each frame independently. Feed normalized grayscale to KLT and the corresponding 8-bit grayscale to centroiding.

### Space-aware KLT and summaries

- Use `CFrontendKltPipeline` with `illumination_mask.enabled=true` and `EFeatureSelectionPolicy::KmeansCoverage` by default in both programs. Keep its track IDs, history, feature replenishment, and extraction-retry policy. Do not feed renderer hit/depth masks or other simulation truth into KLT. Install no static occlusion mask by default.
- Treat `EmptyForeground` and zero new features as normal frame outcomes. Display mask status, eligible fraction, retry, and active/tracked/new/lost counts. For ordinary non-space webcam footage, allow explicit launch-time `--klt-extraction generic`; all space-scene acceptance uses the default `space` setting.
- Use GLFW/OpenGL on the main thread. Left-drag orbits, right-drag pans, wheel changes distance, arrows move in the camera plane, `R` restores the pose, and `Esc` exits. Keep renderer/KLT/centroiding on one worker. The camera program has a capture worker and a processing worker. One-slot latest-frame mailboxes bound backlog; publish only after every enabled result belongs to that frame.
- Draw colored KLT points and short trails, a distinct centroid crosshair, and YOLO boxes. Match nav-frontend's frame-summary presentation with a dark text band, without importing MATLAB code or unrelated LiDAR fields. Include frame ID, source timestamp, KLT counts, mask/retry, centroid `OFF`/`OK`/`OUTSIDE`/`ERROR` and coordinates, YOLO count when enabled, per-stage times, and dropped frames. Use one per-frame record for overlay text, console output, and saved `frames.jsonl`.
- Save separate annotated PNGs for processed frames and `run.json` only under explicit `--output-dir`. Interactive use writes no output by default; never write into Spectra-RT's benchmark output tree.

### Models and sources

- Require exactly one camera source: `--camera-index`, `--video`, or `--frames-dir`. Naturally sort folder images, reject inconsistent dimensions, pace file sources as streams, and drain processing at EOF. Keep source and processed indices distinct.
- Use AutoForge's plain image-only centroid role with one image input and one `[1,2]` normalized output. Map to source pixels without silently clamping out-of-frame values. `centroid` and `both` require `DEMO_ENABLE_ML=ON` and `--centroid-model` or fail clearly at startup.
- Decode YOLOv7 640 x 640 raw detections, filter by score, apply class-aware NMS, and map boxes to source pixels. Keep model weights external. Centroiding defaults to CPU; YOLO follows its manifest. CUDA inference uses physical GPU 1 under `CUDA_VISIBLE_DEVICES=1`.

## Stages

### 0. Plan and goal

- [x] Create this plan before template export or source implementation.
- [x] Start the goal for the two programs, GPU 1 validation, review, documentation, and demo-only commits.
- [x] Record the plan path and baseline external checkout status for review.

### 1. Tailor and build

- [x] Export only needed files from template commit `f207d2a`; omit ROS, wrappers, generic tests, CUDA placeholders, and unrelated tooling. Keep minimal CMake, focused instructions, `.clang-format`, README, source, and behavior tests.
- [x] Build/install Spectra-RT and KLT out of source into a demo-local prefix using one OpenCV installation. Record dependency revisions and dirty-state digests; do not edit external source.
- [x] Set `CUDA_VISIBLE_DEVICES=1`, compile for architecture 89, and verify logical device 0 is the RTX 4070 Ti SUPER.
- [x] Review, stage explicit paths, and commit the minimal project.

### 2. Required rendered KLT and summaries

- [x] Implement a small achromatic illuminated-body test scene, then load untextured Bennu through the same factorized physical sensor path.
- [x] Implement Bayer readback, reconstruction, fixed-scale KLT input, space-aware extraction, GLFW controls, colored tracks, frame-summary text, timing, and finite headless operation.
- [x] Verify `Ready` masks and persistent IDs under camera motion. Test `EmptyForeground`, pending retry, and recovery on a dark/body-return sequence.
- [x] Add an eight-position red-to-yellow display trail for surviving KLT IDs and save a ten-frame Bennu MP4.
- [x] Add continuous nominal-rate Bennu spin, a launch multiplier, fixed-Sun phase-angle diagnostics, and a launch camera azimuth.
- [x] Verify changing body transforms with a fixed camera and a separate low-to-high phase-angle camera sweep on GPU 1. Save a measured-output overlay MP4 at a readable playback rate.
- [x] Review and commit the rendered-KLT capability using the commit style below.

### 3. Webcam, video, and frames folder

- [x] Implement three sources with default space-aware KLT, the same preview, and the same per-frame summary.
- [x] Generate moving space-like folder fixtures in the behavior test. Verify natural order, EOF, mask outcomes, and ID continuity.
- [x] Run a disposable MJPG video fixture and verify bounded dropping with distinct source and processed indices.
- [x] Launch both previews under Xvfb and review the control mapping. Document that a real PC webcam remains untested here.
- [ ] Exercise mouse and arrow events on a physical interactive window; only Xvfb startup and deterministic orbit motion were tested here.
- [x] Review and commit the camera/file stream capability.

### 4. Centroiding, combined mode, and YOLOv7

- [x] Add optional AutoForge linkage. Implement rendered `centroid` and `both` modes; confirm one render produces one matching KLT result, centroid result, and combined summary.
- [x] Run centroiding on video and folder sources, alone and with KLT.
- [ ] Run centroiding on a physical webcam; no webcam device is available here.
- [x] Add YOLOv7 to camera streams; test decoding, NMS, coordinate restoration, and combined summaries.
- [x] Record the actual backend. No model needed an external source edit; KLT paths continued.
- [x] Review and commit the working model integrations with the shared stream source.

### 5. Textured Bennu external-repo gate

- [x] Prepare a narrow patch against Spectra-RT `f948bd6`: admit valid single-channel scalar textures for achromatic Lambertian/Lommel-Seeliger factorized materials; retain RGB/RGBA and chromatic rejection. Update focused GPU tests and diagnostics.
- [x] Review the patch and stop before changing Spectra-RT source. Preserve its unrelated `scripts/quick_demo/preview_model.sh` edit.
- [ ] After approval, apply without a Spectra-RT commit, rebuild the demo-local install, and test textured factorized output on GPU 1.
- [ ] Run textured Bennu in `klt`, `centroid`, and `both` where the model works. Keep patch, base revision, instructions, and evidence in the demo repo.

### 6. Final verification and handoff

- [x] Run fresh configure/build, focused tests, finite rendered/media smokes, Xvfb preview startup, and GPU 1 identity checks.
- [x] Measure geometry load separately from render, readback, reconstruction, KLT/mask, centroid, YOLO, preview, and dropped frames. Report eight- and 32-sample runs separately; make no unmeasured FPS claim.
- [x] Document quick-start commands, modes, controls, asset/model paths, resolved-body limits of space-aware extraction, camera limitations, and deferred models.
- [x] Review full final diff and plan state; commit remaining demo-only work. Do not push or open a PR.

## Review, commit, and comment rules

After every large stage, inspect the full candidate diff and external worktree statuses; check threading, allocations, error paths, public Doxygen, physical-unit names, comments, and readability. Run relevant builds/tests, `clang-format`, and `git diff --check`. Stage explicit paths, inspect the full index, and run `git diff --cached --check` before committing. Opus 5.5 is not exposed here; use a focused guideline-based review.

Follow Spectra-RT's commit style in this demo repo only: imperative sentence-case subject around 50-70 characters, no final period, no `feat:`/`fix:`/`docs:` prefix; `[MAJOR]` for significant capability, `[BUGFIX]` for correctness, `[HOTFIX]` for urgent narrow repair, no tag for routine work. Optional body bullets start with imperative verbs, have blank lines between them, and have no terminal periods. Describe behavior and consequence rather than file inventories. Add no AI attribution or `Co-Authored-By`. Run a no-ai-slop wording pass before each commit.

The source batch used `[MAJOR] Stream renderer and camera frames through perception`; its optional body names the KLT streams, model overlays, and Bennu rotation. The documentation and patch artifact use a separate plain imperative subject. Use no automatic attribution trailers.

Example rendered-KLT body:

```text
- Reconstruct Bayer measurements for tracking and display

- Keep detector resolution fixed while the preview window resizes

- Report mask retries and persistent KLT track IDs
```

Write brief imperative code comments for non-obvious steps and invariants, such as “Keep raw electrons unchanged before display scaling” and “Publish only after both results belong to this frame”. Group code by purpose; do not narrate obvious statements. Document public APIs with Doxygen.

Stop for critical changes to the physical sensor, factorized path, KLT ownership, two-program design, or Bennu interpretation. Stop before any external-repo source edit. Ordinary demo-repo fixes may proceed; model-specific blockers may be deferred as stated above. Commit only in this repo and never push.

## Baseline at plan start (2026-09-24)

This plan is `/home/peterc/devDir/rendering-to-perception-demos-open-night2026/PLAN.md`. The external checkouts below had no staged changes. Counts are `git status --short` entries; hashes are SHA-256 of the unstaged diff, recorded to help detect accidental changes during integration.

| Checkout | HEAD | Entries | Unstaged diff SHA-256 |
| --- | --- | ---: | --- |
| `dev-tools/cpp_cuda_template_project` | `f207d2a` | 0 | empty |
| `rendering-sw/spectra-rt` | `f948bd6` | 1 | `97b5c279bb247270037ae94f84396800b8ea5413cfbb460532120ccace61390c` |
| `SLAM-repos/pyramidal-klt-for-space-nav` | `c714e1f` | 28 | `4f47f8e18f1a27cbd72ffdc853c358049d9de57fa43c8c7c84be3badae2c233f` |
| `SLAM-repos/slam-primitives` | `5e54f81` | 42 | `9abb5bce9bb5e90270749b0248f641c7020565b8a275d686cbbf1afc2628916c` |
| `ML-repos/torchAutoForge-deploy` | `03bb25c` | 1 | `a7f6a7b21eb2cb82d976e5521b7994323a31421b160d96941402bae6670ada6c` |

## Execution and review record (2026-09-24)

### Build and source ownership

- Installed Spectra-RT `f948bd6`, KLT `c714e1f`, and AutoForge `03bb25c` from out-of-source builds under ignored `build/` into ignored `deps/`. Used OpenCV 4.10 from `/usr/local`; Spectra-RT used CUDA architecture 89 and OptiX. A fresh `build/demo-fresh` ML-enabled configure/build and CTest pass, and a separate `build/demo-no-ml` configure/build pass.
- `nvidia-smi` identifies physical GPU 1 as RTX 4070 Ti SUPER. Every renderer smoke used `CUDA_VISIBLE_DEVICES=1`; renderer logs confirm logical device 0 is that GPU.
- Rechecked all five external statuses and unstaged diff hashes against the baseline table. Each count and hash was unchanged. No external source, gitlink, commit, or stage was modified. Only the template license entered this repo.
- Stage 1 review: inspected the minimal exported file set, package links, include collision, Eigen allocation ABI, build options, C++ format, and fresh builds. The render target matches Spectra-RT's Eigen allocation setting; named logger headers resolve the KLT/Spectra include collision in this repo.

### Rendered KLT and combined mode

- `build/final_bennu_both`: full untextured Bennu, three 2048 x 1536 frames at 8 spp with a 0.1-degree orbit, KLT and centroid together. Mask was `READY` then `WAIT`; 150 of 150 IDs survived both moves; centroid was `OK` on each frame; no drops. Inspected an annotated PNG.
- `build/evidence_bennu_trails`: ten 8-spp Bennu frames with 0.25-degree orbit. Every preceding active ID survived each next frame. KLT replenished from 150 to 300 active points at frame 5; centroid stayed `OK`. Encoded and probed `build/evidence_bennu_trails.mp4`: H.264, 2048 x 1536, ten frames, 2.5 seconds at chosen 4 fps playback. Inspected frame 9; the eight-position display trails fade red to yellow. This is a recording of processed stream frames, not a measured live playback rate.
- `build/retry_smoke` and `camera_stream_contract`: two dark frames reported `EMPTY` with retry pending; a lit frame reported `READY` and admitted features, then tracked them. No renderer truth mask enters the pipeline.
- Stage 2 review: checked factorized `SENSOR_MEASUREMENT`, Bayer raw readback and reconstruction, fixed 6300-electron scale, space-aware KLT defaults, track-ID alignment, mask/retry handling, scene units, motion, preview, and visual output. The eight-position cache holds display points only and discards missing IDs; KLT owns actual tracks.

### Camera, models, and preview

- `ctest --test-dir build/demo-fresh --output-on-failure`: `camera_stream_contract` passed. It generates disposable moving folder frames, checks natural order by image markers, five matching source/processed IDs, persistent KLT IDs, dark-frame retry/recovery, and dimension-change rejection.
- `build/camera_video_smoke` exercised MJPG video at 120 fps: three capture frames were superseded while processing kept distinct source and processed IDs. `build/final_camera_both` exercised folder KLT+centroid; `build/video_both_smoke` exercised video KLT+centroid. `build/camera_centroid_smoke` exercised centroid-only folder mode. No physical webcam was available.
- `build/final_video_all` exercised KLT+centroid+YOLO on video. `build/final_yolo_dog` exercised the YOLO decoder/NMS with three visible boxes on a local dog/bicycle/car photo; inspected the annotated PNG. Its manifest reported `requested_targets=cuda,cpu;applied_ort_providers=cuda,cpu;device_id=0` under `CUDA_VISIBLE_DEVICES=1`. Centroid reported CPU provider. This proves runnable inference and overlays on these inputs, not detection accuracy.
- `build/final_timed_camera_xvfb.log` and `build/final_timed_render_xvfb.log` show both preview programs started and exited under Xvfb. With no display, camera preview now exits cleanly with `Cannot initialize GLFW`; worker threads had not started. Physical mouse/key input events remain untested.
- Stage 3 and 4 review: inspected one-slot capture and preview mailboxes, EOF drain, input validation, source/processed identity, model tensor layout, original-image YOLO input, class-aware NMS, coordinate mapping, centroid status, small-frame text fit, and Xvfb output.

### Timings and external patch gate

- `build/final_timed_bennu_8`: one full Bennu frame at 8 spp. Scene load/build 16212.1 ms; render 27.1 ms; sensor readback 2.0 ms; reconstruction and fixed scaling 13.2 ms; KLT 77.8 ms; centroid 30.8 ms; zero drops. The separate earlier three-frame run establishes track persistence.
- `build/final_timed_sphere_8`: three 8-spp frames; after first-frame work, render 9.4 ms, readback 2.0 ms, reconstruction 14.0-14.2 ms, KLT 78.0-79.4 ms, centroid 37.8-39.5 ms. `build/final_timed_sphere_32`: one 32-spp reference; render 42.0 ms, readback 2.0 ms, reconstruction 14.4 ms, KLT 97.8 ms. These are stage times, not end-to-end FPS claims.
- Interactive Xvfb preview upload/draw/swap averaged 13.9 ms for two rendered frames and 5.6 ms for two camera frames. The YOLO dog first inference took 487.0 ms; a second video frame took 15.5 ms after warmup. These observations are input- and run-specific.
- `--albedo-jpeg` decoded the full 15708 x 7854 Bennu JPEG and reached the expected current Spectra-RT error: `Factorized transport requires achromatic material coefficients and no albedo textures.` The prepared patch adds scalar-only admission and a focused GPU comparison test; RGB/RGBA rejection remains. `git apply --check patches/0001-admit-scalar-albedo-in-factorized-transport.patch` passes against Spectra-RT `f948bd6`. No patch was applied there, so textured output and its GPU test remain pending user approval for that external source edit.
- Stage 5 review: inspected texture conversion, scalar upload behavior, material admission and update paths, diagnostics, and the focused test in the patch. Stage 6 review: ran `clang-format --dry-run --Werror`, CTest, fresh ML and no-ML builds, JSON/PNG checks, FFprobe, external dirty-state recheck, and the full source/documentation readability pass. After the rotation change, rebuilt ML and no-ML targets, reran CTest, rejected invalid spin and azimuth arguments, checked static sphere metadata, and reviewed the phase-sweep frames. Both source and documentation batches passed complete staged-diff checks.

### Commits

- `f5b6150` `[MAJOR] Stream renderer and camera frames through perception`: staged 14 explicit source/config/test paths, reviewed the complete index, passed `git diff --cached --check`, and committed only this repo. The source programs share CMake and `demo_core`, so their functioning integration formed one cohesive source batch; the plan and operating instructions follow separately.
- `38cf534` `Document demo operation and prepare scalar-albedo patch`: staged only `.gitattributes`, this plan, README, and the unapplied patch; reviewed their complete diff and passed `git diff --cached --check`. The path-specific attribute excludes inherent unified-diff context spaces from that whitespace check. The patch still passes `git apply --check` at Spectra-RT `f948bd6` and remains unapplied.
- The final plan-ledger commit marks the completed local review and keeps physical webcam, physical input-event, and textured-Bennu checks unchecked until they can run.
- `0da0841` `[BUGFIX] Use spacecraft-measured Bennu rotation period`: replaced the older radar period with the OSIRIS-REx 2018 spacecraft measurement, rebuilt ML and no-ML variants, reran CTest, regenerated the GPU 1 phase-sweep clip, and reviewed the three-file staged diff.

### Requested rotation and phase-angle extension

- [OSIRIS-REx approach lightcurves](https://www.nature.com/articles/s41467-019-09213-x) measured Bennu's sidereal period as 4.296007 hours in 2018 and found a gradual spin-up. [NASA PDS](https://pds.nasa.gov/ds-view/pds/viewProfile.jsp?dsid=EAR-A-I0037-5-BENNUSHAPE-V1.0) identifies the model `+Z` axis as the spin pole. The camera sweep changes observation phase while the finite Sun remains fixed in world space. The run uses the measured period as a fixed reference value and does not claim a dated Bennu orientation or orbital ephemeris.
- `build/spin_smoke`: three untextured Bennu frames on GPU 1 with fixed camera azimuth 45 degrees and 3000x spin. Frame diagnostics kept the Sun–body–camera phase at 12.0 degrees while body phase moved from 0.0 to 31.1 degrees. KLT kept all 150 IDs. Renderer code reuses geometry and updates the instance acceleration structure.
- `build/evidence_bennu_phase_sweep`: 50 untextured Bennu frames on GPU 1, KLT and centroiding together, camera start 45 degrees, 3 degrees per frame, body spin 250x. `run.json` records the 15465.6252-second reference period. Phase angle rose monotonically from 12.0 to 145.1 degrees; body phase rose from 0.0 to 61.8 degrees; centroid status was `OK` throughout; KLT had 288 active tracks at frame 49; zero dropped frames. Median post-first-frame scene update was 0.286 ms. Inspected the final frame for lighting, colored trails, centroid crosshair, and diagnostic text. Encoded `build/evidence_bennu_phase_sweep.mp4` as H.264, 2048 x 1536, 50 frames, 8.33 seconds at an intentionally chosen 6 fps playback rate. This MP4 records real processed frames; the playback rate is independent of the wall-time spin rate.
- The regenerated MP4 is 3,480,602 bytes with SHA-256 `8d200643c8824464905fc8a77dbe577dc85f12e154f2093e03af365b5be76e10`. It lives under ignored `build/` and remains available locally.
