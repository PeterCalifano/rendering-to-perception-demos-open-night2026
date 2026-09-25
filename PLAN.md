# Rendering-to-perception demos

## Goal and acceptance

Build two C++20 streaming programs in this repo:

| Program | Source | Modes |
| --- | --- | --- |
| `render_stream_demo` | Spectra-RT factorized sensor frames | `klt`, `centroid`, `both` |
| `camera_stream_demo` | Webcam, video, or image folder | `klt`, `centroid`, `both`, with optional YOLOv7 |

`both` processes each source frame with KLT and centroiding and publishes one combined preview. The first milestone is physically rendered frames with space-aware KLT tracks that retain IDs across camera motion. Attempt centroiding and YOLOv7 with available models; record any runtime blocker and continue with the KLT programs. No physical webcam is available here, so use video and folder inputs for acceptance checks.

Before editing source, write this plan, start the goal, and record the current state of every external checkout. Update checkboxes, commands, results, review findings, deferrals, and commit hashes as work advances. A checked item requires evidence. Stop for user review before an external repo source edit; the scalar-texture exception below received that approval on September 25.

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
- Decode YOLOv7 640 x 640 raw detections, filter by score, apply class-aware NMS, and map boxes to source pixels. Keep model weights external. Centroiding defaults to CPU; YOLO follows its manifest. When selecting a GPU, use `CUDA_VISIBLE_DEVICES` for that run; the demo must not require a particular physical index or model name.

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
- [x] Enable the KLT frontend's calibrated MSAC rejection for rendered frames using the WFOV ray geometry. Record model status and rejected tracks in the shared summary; keep uncalibrated camera/video inputs explicit.
- [x] Check low-motion track retention and the high-phase sweep on GPU 1 with MSAC enabled. Review the updated overlay and save a separate filtered evidence clip.
- [x] Review and commit the rendered-KLT capability using the commit style below.

### 3. Webcam, video, and image folders

- [x] Implement three sources with default space-aware KLT, the same preview, and the same per-frame summary.
- [x] Generate moving space-like folder fixtures in the behavior test. Verify natural order, EOF, mask outcomes, and ID continuity.
- [x] Run a disposable MJPG video fixture and verify bounded dropping with distinct source and processed indices.
- [x] Launch both previews under Xvfb and review the control mapping. Document that a real PC webcam remains untested here.
- [ ] Exercise mouse and arrow events on a physical interactive window; injected Xvfb events passed, but no physical display is available here.
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
- [x] After approval, apply without a Spectra-RT commit, rebuild the demo-local install, and test textured factorized output on GPU 1.
- [x] Run textured Bennu in `klt`, `centroid`, and `both`. Keep patch, base revision, instructions, and evidence in the demo repo.

### 6. Final verification and handoff

- [x] Run fresh configure/build, focused tests, finite rendered/media smokes, Xvfb preview startup, and GPU 1 identity checks.
- [x] Measure geometry load separately from render, readback, reconstruction, KLT/mask, centroid, YOLO, preview, and dropped frames. Report eight- and 32-sample runs separately; make no unmeasured FPS claim.
- [x] Document quick-start commands, modes, controls, asset/model paths, resolved-body limits of space-aware extraction, camera limitations, and deferred models.
- [x] Review full final diff and plan state; commit remaining demo-only work. Do not push or open a PR.
- [x] Review the September 25 scalar-texture and MSAC diffs, rebuild both demo variants, and leave all new edits unstaged at the original handoff.
- [x] On later authorization, commit the MSAC source and documentation batches only in this demo repo. Leave the Spectra-RT source patch unstaged and make no push.

### 7. Local copy bundle (2026-09-25)

- [x] Inventory all runtime library dependencies, installed CMake packages, Bennu assets, and the supporting files for both models.
- [x] Copy installed native libraries and executables into ignored `external/`; copy Bennu and model payloads into ignored `assets/` with YOLO manifest-relative paths preserved.
- [x] Add a launcher that resolves copied libraries before embedded absolute RUNPATH entries and sets the copied Bennu data root.
- [x] Test relocated sphere+centroid and camera KLT+centroid+YOLO on physical GPU 1; confirm CPU centroid and CUDA YOLO providers.
- [x] Test relocated textured Bennu, copied CMake package rebuild, checksum manifest, and loader path evidence; record exact results in the handoff.
- [x] Review scripts, documentation, copied payload, host prerequisites, and final diff; commit only demo-repo paths under the existing demo-only authorization. Do not push.

### 8. Portable GPU selection and second-machine build (2026-09-25)

- [x] Remove the hard-coded GPU 1/4070 Ti checks in both executables. Report the selected logical CUDA device and actual name; replace the false physical-index value in rendered `run.json` with device facts.
- [x] Leave `CUDA_VISIBLE_DEVICES` unset by default in the bundle launcher and allow it to launch either copied or newly built executables with the copied library path.
- [x] Rebuild on the local machine, select the 4070 Ti through `CUDA_VISIBLE_DEVICES=1`, and inspect the one-frame sphere output and `run.json` device facts.
- [x] Transfer the changed source and launcher to `peterc-alien16x`, verify bundle checksums, rebuild with its CUDA 12.9 toolkit, and run bounded render and camera/model smokes on its compute-12.0 GPU.
- [x] Review the five-file Spectra-RT scalar-texture diff and focused GPU 1 physical checks. The approved patch was committed and pushed as `fdf46db` before the later review-first instruction; all other dirty Spectra-RT files remain unstaged.
- [x] Update the README and development handoff with the device contract and second-machine evidence. Review readability, formatting, tests, and the final diff before any staging.

### 9. Reword demo history and review remaining changes (2026-09-25)

- [x] Reword and re-sign all ten existing demo commits with `(Codex)` at the end of each title. Verify every message body and file tree against the original and preserve the dirty working tree.
- [x] Review the GPU selection source, launcher, plan, README, and handoff for simplification, documentation accuracy, formatting, and test evidence; stage only an explicit demo-repo path batch.
- [x] Obtain explicit authorization to replace the published demo history and sync `peterc-alien16x`; the user granted it on 2026-09-25.
- [x] Push the signed history with an exact lease, verify GitHub `main`, and sync the second machine from GitHub.

### 10. One-command demo build (2026-09-25)

- [x] Add a root `build.sh` that configures and builds both ML-enabled programs from the copied `external/` packages, without rebuilding external repositories.
- [x] Document host prerequisites, the script's options, and how to launch its binaries through the bundled library path.
- [x] Run shell checks, a real configure/build, optional behavior test, and a bounded runtime smoke; review the complete staged diff.
- [ ] Commit the demo-only build workflow and push it under the user's explicit request; verify the published head.

### 11. Live stage logging (2026-09-25)

- [x] Log completed frame status and labeled stage times in both programs through the KLT logger facility, reusing the existing summary measurements.
- [x] Document the terminal log level and timing boundaries; verify a rendered and a camera frame, then review the source diff for readability.
- [ ] Commit the logging capability separately from the build script and push both commits under the user's explicit request.

`./build.sh --tests --jobs 8` configured and built both binaries from `external/` and passed
`camera_stream_contract` (1/1). `bash -n`, `shellcheck`, argument rejection, and the
C++ `clang-format` check passed. On GPU 1, a one-frame sphere run logged render,
readback, reconstruction, KLT, and centroid stages; a one-frame folder run logged
capture, KLT, centroid, and YOLO stages. `DEMO_LOG_LEVEL=quiet` suppressed the
demo frame lines. The copied libraries and assets stayed in ignored directories.

## Review, commit, and comment rules

After every large stage, inspect the full candidate diff and external worktree statuses; check threading, allocations, error paths, public Doxygen, physical-unit names, comments, and readability. Run relevant builds/tests, `clang-format`, and `git diff --check`. Stage explicit paths, inspect the full index, and run `git diff --cached --check` before committing. Opus 5.5 is not exposed here; use a focused guideline-based review.

Follow Spectra-RT's commit style in this demo repo only: imperative sentence-case subject around 50-70 characters, no final period, no `feat:`/`fix:`/`docs:` prefix; `[MAJOR]` for significant capability, `[BUGFIX]` for correctness, `[HOTFIX]` for urgent narrow repair, no tag for routine work. Optional body bullets start with imperative verbs, have blank lines between them, and have no terminal periods. Describe behavior and consequence rather than file inventories. The user explicitly requested `(Codex)` at the end of each existing title; add no authorship trailers or `Co-Authored-By`. Run a no-ai-slop wording pass before each commit.

The source batch used `[MAJOR] Stream renderer and camera frames through perception (Codex)`; its optional body names the KLT streams, model overlays, and Bennu rotation. The documentation and patch artifact use a separate plain imperative subject. Use no automatic attribution trailers.

Example rendered-KLT body:

```text
- Reconstruct Bayer measurements for tracking and display

- Keep detector resolution fixed while the preview window resizes

- Report mask retries and persistent KLT track IDs
```

Write brief imperative code comments for non-obvious steps and invariants, such as “Keep raw electrons unchanged before display scaling” and “Publish only after both results belong to this frame”. Group code by purpose; do not narrate obvious statements. Document public APIs with Doxygen.

Stop for critical changes to the physical sensor, factorized path, KLT ownership, two-program design, or Bennu interpretation. Stop before any further external-repo source edit. The approved scalar-texture edit is commit `fdf46db` in Spectra-RT; the remaining wrapper and quick-preview edits are separate. Ordinary demo-repo fixes may proceed; model-specific blockers may be deferred as stated above. Commit only in this repo and never push. The later commit authorization covered the demo repo only.

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
- Before the approved Spectra-RT edit, rechecked all five external statuses and unstaged diff hashes against the baseline table. Each count and hash was unchanged. No external source, gitlink, commit, or stage had been modified. Only the template license entered this repo.
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
- `build/xvfb_controls_final_wy7j9nit`: injected left and right drags, two wheel steps,
  Right and Up arrows, `R`, and `Esc` into the mapped render preview under Xvfb with
  `CUDA_VISIBLE_DEVICES=1`. The 63 saved 4-spp sphere frames and JSONL show phase angles
  of 46.234 degrees initially, 20.172 after orbit, 22.504 after pan, 23.310 after zoom,
  21.366 after Right, 20.660 after Up, and 46.234 after reset. `Esc` exited with status 0.
  Inspected saved overlays. These were virtual X11 events, not physical-window input.
- Stage 3 and 4 review: inspected one-slot capture and preview mailboxes, EOF drain, input validation, source/processed identity, model tensor layout, original-image YOLO input, class-aware NMS, coordinate mapping, centroid status, small-frame text fit, and Xvfb output.

### Timings and external patch gate

- `build/final_timed_bennu_8`: one full Bennu frame at 8 spp. Scene load/build 16212.1 ms; render 27.1 ms; sensor readback 2.0 ms; reconstruction and fixed scaling 13.2 ms; KLT 77.8 ms; centroid 30.8 ms; zero drops. The separate earlier three-frame run establishes track persistence.
- `build/final_timed_sphere_8`: three 8-spp frames; after first-frame work, render 9.4 ms, readback 2.0 ms, reconstruction 14.0-14.2 ms, KLT 78.0-79.4 ms, centroid 37.8-39.5 ms. `build/final_timed_sphere_32`: one 32-spp reference; render 42.0 ms, readback 2.0 ms, reconstruction 14.4 ms, KLT 97.8 ms. These are stage times, not end-to-end FPS claims.
- Interactive Xvfb preview upload/draw/swap averaged 13.9 ms for two rendered frames and 5.6 ms for two camera frames. The YOLO dog first inference took 487.0 ms; a second video frame took 15.5 ms after warmup. These observations are input- and run-specific.
- At the original external-source gate, `--albedo-jpeg` decoded the full 15708 x 7854 Bennu JPEG and reached the former Spectra-RT texture-rejection error. The prepared patch added scalar-only admission and a focused GPU comparison test while retaining RGB/RGBA rejection. The patch was still unapplied at that gate; the user subsequently approved its narrow application on September 25.
- Stage 5 review: inspected texture conversion, scalar upload behavior, material admission and update paths, diagnostics, and the focused test in the patch. Stage 6 review: ran `clang-format --dry-run --Werror`, CTest, fresh ML and no-ML builds, JSON/PNG checks, FFprobe, external dirty-state recheck, and the full source/documentation readability pass. After the rotation change, rebuilt ML and no-ML targets, reran CTest, rejected invalid spin and azimuth arguments, checked static sphere metadata, and reviewed the phase-sweep frames. Both source and documentation batches passed complete staged-diff checks.

### Commits

- `bba4026` `[MAJOR] Stream renderer and camera frames through perception (Codex)`: staged 14 explicit source/config/test paths, reviewed the complete index, passed `git diff --cached --check`, and committed only this repo. The source programs share CMake and `demo_core`, so their functioning integration formed one cohesive source batch; the plan and operating instructions follow separately.
- `dac1c9a` `Document demo operation and prepare scalar-albedo patch (Codex)`: staged only `.gitattributes`, this plan, README, and the then-unapplied patch; reviewed their complete diff and passed `git diff --cached --check`. The path-specific attribute excludes inherent unified-diff context spaces from that whitespace check. At the time, the patch passed `git apply --check` at Spectra-RT `f948bd6`.
- The earlier plan-ledger commit marked the completed local review and kept physical webcam, physical input-event, and textured-Bennu checks unchecked. Textured-Bennu checks were completed after the external edit was approved; the physical-device checks remain open.
- `6fdada5` `[BUGFIX] Use spacecraft-measured Bennu rotation period (Codex)`: replaced the older radar period with the OSIRIS-REx 2018 spacecraft measurement, rebuilt ML and no-ML variants, reran CTest, regenerated the GPU 1 phase-sweep clip, and reviewed the three-file staged diff.

### Requested rotation and phase-angle extension

- [OSIRIS-REx approach lightcurves](https://www.nature.com/articles/s41467-019-09213-x) measured Bennu's sidereal period as 4.296007 hours in 2018 and found a gradual spin-up. [NASA PDS](https://pds.nasa.gov/ds-view/pds/viewProfile.jsp?dsid=EAR-A-I0037-5-BENNUSHAPE-V1.0) identifies the model `+Z` axis as the spin pole. The camera sweep changes observation phase while the finite Sun remains fixed in world space. The run uses the measured period as a fixed reference value and does not claim a dated Bennu orientation or orbital ephemeris.
- `build/spin_smoke`: three untextured Bennu frames on GPU 1 with fixed camera azimuth 45 degrees and 3000x spin. Frame diagnostics kept the Sun–body–camera phase at 12.0 degrees while body phase moved from 0.0 to 31.1 degrees. KLT kept all 150 IDs. Renderer code reuses geometry and updates the instance acceleration structure.
- `build/evidence_bennu_phase_sweep`: 50 untextured Bennu frames on GPU 1, KLT and centroiding together, camera start 45 degrees, 3 degrees per frame, body spin 250x. `run.json` records the 15465.6252-second reference period. Phase angle rose monotonically from 12.0 to 145.1 degrees; body phase rose from 0.0 to 61.8 degrees; centroid status was `OK` throughout; KLT had 288 active tracks at frame 49; zero dropped frames. Median post-first-frame scene update was 0.286 ms. Inspected the final frame for lighting, colored trails, centroid crosshair, and diagnostic text. Encoded `build/evidence_bennu_phase_sweep.mp4` as H.264, 2048 x 1536, 50 frames, 8.33 seconds at an intentionally chosen 6 fps playback rate. This MP4 records real processed frames; the playback rate is independent of the wall-time spin rate.
- The regenerated MP4 is 3,480,602 bytes with SHA-256 `8d200643c8824464905fc8a77dbe577dc85f12e154f2093e03af365b5be76e10`. It lives under ignored `build/` and remains available locally.

## September 25 scalar-texture and geometric-rejection extension

- With explicit user approval, applied the reviewed scalar-texture patch only to Spectra-RT `src/core/CSpectralRaytracer.cpp`, `src/kernels/raytracer_kernels.ptx.cu`, `src/scene/CSceneMaterials.cpp`, `src/scene/CSceneMaterials.h`, and `tests/core/testFactorizedTransport.cpp` on branch `feature/implement-factorized-radiometry-mode` at `f948bd6`. Left every change unstaged and preserved the pre-existing quick-demo script edit. Rebuilt the demo-local Spectra-RT install. On physical GPU 1, `testFactorizedTransport` passed 1,379 assertions in 26 cases and `testSceneMaterials` passed 386 assertions in 19 cases. After the final whitespace edit, rebuilt both test targets and reran the focused scalar-texture case: 17 assertions passed. Grayscale 128/255 scales raw factorized Lambertian and Lommel-Seeliger transport and each of three sensor bands by the same factor; two-, three-, and four-channel textures and chromatic coefficients remain rejected. This checks the factorization contract, not measured Bennu reflectance.
- Ran textured Bennu on GPU 1 with `--albedo-jpeg` in `klt`, `centroid`, and `both` modes. The three-frame combined run kept 150 IDs, reported centroid `OK` on all frames, and reported MSAC `VALID` on both transitions with zero geometric rejections. The standalone KLT and centroid modes also completed. The JPEG's decoded linear luminance multiplies the nominal 0.05 Lambertian coefficient; these inputs are demonstration values.
- Enabled the KLT frontend's essential-matrix MSAC only for rendered KLT frames. The WFOV profile resolves to a centered, zero-skew pinhole with `fx=fy=5840.90918 px`, `cx=1024 px`, and `cy=768 px`; one pixel is the accepted residual threshold. Disposable off-center and NaN-principal-point profiles exited before rendering with the calibration mismatch diagnostic. Unknown webcam/video/folder calibration leaves geometric rejection `OFF`. `WAIT` and `FAIL` perform no geometric rejection; only `VALID` retires rejected IDs. The overlay trail cache follows surviving frontend IDs.
- `build/evidence_bennu_phase_sweep_msac`: 50 untextured Bennu frames at 8 spp with KLT and centroiding, camera azimuth 45 degrees, three-degree orbit steps, and 250x body spin. On GPU 1, phase rose from 11.976712 to 145.126152 degrees. MSAC was `WAIT` on the first frame and `VALID` on 49 transitions, retiring 1,453 correspondences in total; 30 active tracks remained at the final crescent. Centroid was `OK` for all 50 frames; zero frames dropped. Inspected the final annotated PNG. `build/evidence_bennu_phase_sweep_msac.mp4` is H.264, 2048 x 1536, 50 frames, 8.33 seconds at chosen 6 fps playback, SHA-256 `39659398fe8f130007bc04f26992a85b4b2b69e13aed8508dd90e24fdd48b5e8`. These are MSAC model rejections, not labeled false matches; the moving body may also violate rigid static-scene assumptions.
- Rebuilt the ML and no-ML demo variants after the final calibration check. `camera_stream_contract` passed and confirmed `MSAC OFF` for uncalibrated file input. Reviewed the source and documentation diffs, formatting, patch reversibility, output JSON, clip probe, and unstaged worktree boundaries. At the original September 25 handoff, this work had made no new commit, stage, or push.
- During the final worktree check, additional unstaged Spectra-RT edits appeared in `src/core/CSpectralRaytracer.h`, `src/core/CSpectralRaytracerWrapper.cpp`, `src/spectra_rt.i`, and `tests/core/testRayTracerConfig.cpp`. They are outside this patch and were left untouched. The original GPU tests and rendered evidence above predate those edits; the later focused GPU reruns check the two named suites in their presence, not the unrelated wrapper behavior.
- The later demo-only authorization produced `227d931` `[MAJOR] Reject geometric outliers in rendered KLT streams (Codex)` from the five source/test paths. The ML and no-ML builds, CTest, final source diff, and `git diff --cached --check` passed before that commit. Rebuilt the Spectra-RT focused test targets and reran both GPU 1 suites afterward: 1,379 factorized assertions and 386 material assertions passed. The patch artifact exactly matched the five owned Spectra-RT diffs. The documentation and patch artifact formed a separate demo commit; at that handoff the Spectra-RT edits were still unstaged and uncommitted.
- `3d89fca` `Document calibrated KLT filtering and scalar texture checks (Codex)` committed only this plan, README, and the patch artifact after full staged-diff review and `git diff --cached --check`. The user confirmed demo-only commits. The Spectra-RT index was empty at that handoff; the later scalar-texture commit is recorded in Stage 8.

### GPU selection and second-machine evidence

- On the local RTX 4070 Ti SUPER, `CUDA_VISIBLE_DEVICES=1 DEMO_BINARY_DIR=build/demo scripts/run_local_bundle.sh render --scene sphere --spp 1 --max-frames 1 --headless --output-dir build/device_contract_local` completed. `run.json` reports logical device 0, compute 8.9, and the selected GPU name. The local camera contract CTest passed.
- On `peterc-alien16x`, the bundle checksum passed and `build/portable` rebuilt with the transferred source. The launcher ran one headless sphere frame on the RTX 5070 Laptop GPU. Rendered `run.json` uses schema version 2 and reports logical device 0 and compute 12.0; the summary reports 150 active KLT features.
- The same target used that annotated frame as a frame-folder input for a bounded camera smoke. KLT reported 149 active features, centroiding reported `OK`, and YOLO reported one box. Backend diagnostics reported CPU centroid and CUDA/CPU YOLO providers. The annotated input does not measure model accuracy.
- Refreshed the two ignored `external/bin` executables and regenerated `external/BUNDLE.sha256`; only their two checksum rows changed. The target checksum passed after transfer. The default launcher, without `DEMO_BINARY_DIR`, then ran both the sphere and camera/model smokes on the RTX 5070 Laptop GPU.
- The ten demo commit subjects were reworded and re-signed on both local checkouts. Each rewritten commit has the same file tree and message body as its original; the dirty working-tree diff hash remained unchanged on both machines. A temporary allowed-signers file verified all ten signatures on `peterc-alien16x`.
- The user authorized the demo push. An exact-lease update replaced GitHub `main` at `2f06155` with `ae31265`, including the reviewed portability commit. The second machine fetched that published history, matched its working files to the new tree, advanced `main` without discarding files, and passed the bundle checksum with a clean tracked tree.
