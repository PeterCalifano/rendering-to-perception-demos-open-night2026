/**
 * @file render_stream_demo.cpp
 * @brief Stream physical Bayer measurements from Spectra-RT into space-aware KLT.
 */
// Both packages expose utils/logging/CLogger.h; load their named headers before shared APIs.
#include <pyramidal_klt/utils/logging/CLogger.h>
#include <spectra_rt/utils/logging/CLogger.h>

#include "demo_core.h"

#include <GLFW/glfw3.h>
#include <base/CCamera.h>
#include <core/CSpectralRaytracer.h>
#include <radiometry/bayer_reconstruction.h>
#include <scene/CSceneBuilder.h>
#include <scene/CSceneDefinition.h>
#include <scene/SceneMotion.h>
#include <utils/parsing/CRendererConfigParser.h>

#include <cuda_runtime.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using gdt::vec3f;
// Use the OSIRIS-REx 2018 period and the model +Z pole for demonstration spin.
constexpr double bennu_spin_period_s = 4.296007 * 3600.0;
constexpr double radians_per_degree = std::numbers::pi_v<double> / 180.0;

struct SOptions
{
    std::filesystem::path camera_yaml{"config/camera_rgb_wfov/camera.yaml"};
    std::filesystem::path model;
    std::filesystem::path albedo_jpeg;
    std::filesystem::path centroid_model;
    std::filesystem::path output_dir;
    std::string scene{"sphere"};
    std::string view{"whole_body"};
    demo::EMode mode{demo::EMode::Klt};
    std::uint32_t spp{8};
    std::uint32_t max_frames{0};
    float orbit_step_deg{0.0f};
    double camera_azimuth_deg{0.0};
    double spin_multiplier{1.0};
    bool headless{false};
};

double ParseFiniteNumber(const std::string& value, const std::string& option)
{
    std::size_t parsed = 0;
    double number = 0.0;
    try
    {
        number = std::stod(value, &parsed);
    }
    catch (const std::exception&)
    {
        throw std::invalid_argument(option + " requires a finite number");
    }
    if (parsed != value.size() || !std::isfinite(number))
        throw std::invalid_argument(option + " requires a finite number");
    return number;
}

SOptions ParseOptions(int argc, char** argv)
{
    SOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string key(argv[index]);
        if (key == "--help")
        {
            std::cout << "render_stream_demo [--scene sphere|bennu] [--model OBJ]\n"
                         "  [--albedo-jpeg FILE] (Bennu; requires scalar-texture Spectra-RT)\n"
                         "  [--view whole_body|approach|surface] [--camera-yaml FILE]\n"
                         "  [--mode klt|centroid|both] [--centroid-model ONNX]\n"
                         "  [--spp N] [--max-frames N]\n"
                         "  [--camera-azimuth-deg N] [--orbit-step-deg N]\n"
                         "  [--spin-multiplier N] [--headless] [--output-dir DIR]\n";
            std::exit(0);
        }
        if (key == "--headless")
        {
            options.headless = true;
            continue;
        }
        if (++index == argc)
            throw std::invalid_argument("Missing value for " + key);
        const std::string value(argv[index]);
        if (key == "--camera-yaml")
            options.camera_yaml = value;
        else if (key == "--model")
            options.model = value;
        else if (key == "--albedo-jpeg")
            options.albedo_jpeg = value;
        else if (key == "--centroid-model")
            options.centroid_model = value;
        else if (key == "--output-dir")
            options.output_dir = value;
        else if (key == "--scene")
            options.scene = value;
        else if (key == "--view")
            options.view = value;
        else if (key == "--spp")
            options.spp = demo::ParsePositiveCount(value, key.c_str());
        else if (key == "--max-frames")
            options.max_frames = demo::ParsePositiveCount(value, key.c_str());
        else if (key == "--orbit-step-deg")
            options.orbit_step_deg = static_cast<float>(ParseFiniteNumber(value, key));
        else if (key == "--camera-azimuth-deg")
            options.camera_azimuth_deg = ParseFiniteNumber(value, key);
        else if (key == "--spin-multiplier")
            options.spin_multiplier = ParseFiniteNumber(value, key);
        else if (key == "--mode")
        {
            if (value == "klt")
                options.mode = demo::EMode::Klt;
            else if (value == "centroid")
                options.mode = demo::EMode::Centroid;
            else if (value == "both")
                options.mode = demo::EMode::Both;
            else
                throw std::invalid_argument("Unknown processing mode: " + value);
        }
        else
            throw std::invalid_argument("Unknown option: " + key);
    }
    if (options.scene != "sphere" && options.scene != "bennu")
        throw std::invalid_argument("--scene must be sphere or bennu");
    if (!options.albedo_jpeg.empty() && options.scene != "bennu")
        throw std::invalid_argument("--albedo-jpeg requires --scene bennu");
    if (options.view != "whole_body" && options.view != "approach" && options.view != "surface")
        throw std::invalid_argument("Unknown view preset");
    if (options.headless && options.max_frames == 0)
        throw std::invalid_argument("Headless rendering requires --max-frames");
    if (!std::isfinite(options.orbit_step_deg) || std::abs(options.orbit_step_deg) > 5.0f)
        throw std::invalid_argument("--orbit-step-deg must be finite and within +/-5 degrees");
    if (std::abs(options.camera_azimuth_deg) > 180.0)
        throw std::invalid_argument("--camera-azimuth-deg must be within +/-180 degrees");
    if (options.spin_multiplier < 0.0 || options.spin_multiplier > 1.0e6)
        throw std::invalid_argument("--spin-multiplier must be within 0 and 1000000");
    if (options.mode != demo::EMode::Klt && options.centroid_model.empty())
        throw std::invalid_argument("Centroid and both modes require --centroid-model");
    if (options.mode == demo::EMode::Klt && !options.centroid_model.empty())
        throw std::invalid_argument("Use --mode both to run KLT and centroiding together");
#ifndef DEMO_ENABLE_ML
    if (options.mode != demo::EMode::Klt)
        throw std::invalid_argument("Models require DEMO_ENABLE_ML=ON");
#endif
    if (options.scene == "bennu" && options.model.empty())
    {
        const char* data_root = std::getenv("RENDERING_DATA");
        if (!data_root)
            throw std::invalid_argument("Bennu needs --model or RENDERING_DATA");
        options.model = std::filesystem::path(data_root) /
                        "assets/bodies/bennu/shape/Bennu_17M_withMtl_split2.obj";
    }
    return options;
}

struct SCameraControl
{
    std::mutex mutex;
    float azimuth_rad{0.0f};
    float elevation_rad{0.0f};
    float distance_km{3.0f};
    float pan_x_km{0.0f};
    float pan_y_km{0.0f};
    float pan_z_km{0.0f};
    double cursor_x_px{0.0};
    double cursor_y_px{0.0};
    bool dragging_left{false};
    bool dragging_right{false};
};

void ResetView(SCameraControl& control, const SOptions& options)
{
    std::lock_guard lock(control.mutex);
    control.azimuth_rad = static_cast<float>(options.camera_azimuth_deg * radians_per_degree);
    control.elevation_rad = options.view == "surface" ? std::atan2(0.072f, 0.36f) : 0.0f;
    control.distance_km = options.view == "whole_body" ? 3.0f
                          : options.view == "approach" ? 1.2f
                                                       : std::hypot(0.36f, 0.072f);
    control.pan_x_km = options.view == "surface" ? 0.24f : 0;
    control.pan_y_km = 0;
    control.pan_z_km = options.view == "surface" ? 0.04f : 0;
}

struct SPose
{
    vec3f position_km;
    vec3f target_km;
};

SPose ReadPose(SCameraControl& control)
{
    std::lock_guard lock(control.mutex);
    const float cos_elevation = std::cos(control.elevation_rad);
    const vec3f target_km{control.pan_x_km, control.pan_y_km, control.pan_z_km};
    return {{target_km.x + control.distance_km * cos_elevation * std::cos(control.azimuth_rad),
             target_km.y + control.distance_km * cos_elevation * std::sin(control.azimuth_rad),
             target_km.z + control.distance_km * std::sin(control.elevation_rad)},
            target_km};
}

void ShiftVertical(SCameraControl& control, float shift_km)
{
    // Move along the projected world-up direction in the current camera plane.
    const float sin_elevation = std::sin(control.elevation_rad);
    control.pan_x_km -= sin_elevation * std::cos(control.azimuth_rad) * shift_km;
    control.pan_y_km -= sin_elevation * std::sin(control.azimuth_rad) * shift_km;
    control.pan_z_km += std::cos(control.elevation_rad) * shift_km;
}

void MouseButton(GLFWwindow* window, int button, int action, int)
{
    auto& control = *static_cast<SCameraControl*>(glfwGetWindowUserPointer(window));
    std::lock_guard lock(control.mutex);
    glfwGetCursorPos(window, &control.cursor_x_px, &control.cursor_y_px);
    if (button == GLFW_MOUSE_BUTTON_LEFT)
        control.dragging_left = action == GLFW_PRESS;
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        control.dragging_right = action == GLFW_PRESS;
}

void CursorMove(GLFWwindow* window, double x_px, double y_px)
{
    auto& control = *static_cast<SCameraControl*>(glfwGetWindowUserPointer(window));
    std::lock_guard lock(control.mutex);
    const float dx_px = static_cast<float>(x_px - control.cursor_x_px);
    const float dy_px = static_cast<float>(y_px - control.cursor_y_px);
    if (control.dragging_left)
    {
        control.azimuth_rad += 0.005f * dx_px;
        control.elevation_rad = std::clamp(control.elevation_rad - 0.005f * dy_px, -1.45f, 1.45f);
    }
    if (control.dragging_right)
    {
        const float shift_km = -0.001f * control.distance_km * dx_px;
        control.pan_x_km -= std::sin(control.azimuth_rad) * shift_km;
        control.pan_y_km += std::cos(control.azimuth_rad) * shift_km;
        ShiftVertical(control, 0.001f * control.distance_km * dy_px);
    }
    control.cursor_x_px = x_px;
    control.cursor_y_px = y_px;
}

void Scroll(GLFWwindow* window, double, double offset)
{
    auto& control = *static_cast<SCameraControl*>(glfwGetWindowUserPointer(window));
    std::lock_guard lock(control.mutex);
    control.distance_km = std::clamp(
        control.distance_km * std::exp(-0.12f * static_cast<float>(offset)), 0.30f, 12.0f);
}

void PollKeyboard(GLFWwindow* window, SCameraControl& control, const SOptions& options)
{
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
        ResetView(control, options);
    const float step_km = 0.003f;
    std::lock_guard lock(control.mutex);
    const float horizontal = static_cast<float>(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) -
                             static_cast<float>(glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS);
    control.pan_x_km -= std::sin(control.azimuth_rad) * horizontal * step_km;
    control.pan_y_km += std::cos(control.azimuth_rad) * horizontal * step_km;
    const float vertical = static_cast<float>(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) -
                           static_cast<float>(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
    ShiftVertical(control, vertical * step_km);
}

spectra_rt::image_io::SImageData LoadLinearAlbedo(const std::filesystem::path& jpeg)
{
    const cv::Mat encoded = cv::imread(jpeg.string(), cv::IMREAD_UNCHANGED);
    if (encoded.empty() || encoded.depth() != CV_8U ||
        (encoded.channels() != 1 && encoded.channels() != 3))
        throw std::invalid_argument("Albedo JPEG must decode as grayscale8 or BGR8");

    std::array<float, 256> srgb_to_linear{};
    for (std::size_t value = 0; value < srgb_to_linear.size(); ++value)
    {
        const float encoded_value = static_cast<float>(value) / 255.0f;
        srgb_to_linear[value] = encoded_value <= 0.04045f
                                    ? encoded_value / 12.92f
                                    : std::pow((encoded_value + 0.055f) / 1.055f, 2.4f);
    }

    spectra_rt::image_io::SImageData linear;
    linear.width = static_cast<std::uint32_t>(encoded.cols);
    linear.height = static_cast<std::uint32_t>(encoded.rows);
    linear.channels = 1;
    linear.scalarType = spectra_rt::image_io::EImageScalarType::UINT8;
    linear.rowStrideBytes = linear.width;
    linear.storage.resize(static_cast<std::size_t>(linear.width) * linear.height);
    for (int row = 0; row < encoded.rows; ++row)
    {
        const auto* source = encoded.ptr<std::uint8_t>(row);
        auto* destination =
            linear.storage.data() + static_cast<std::size_t>(row) * linear.rowStrideBytes;
        for (int column = 0; column < encoded.cols; ++column)
        {
            float luminance = 0.0f;
            if (encoded.channels() == 1)
                luminance = srgb_to_linear[source[column]];
            else
            {
                const auto* pixel = source + 3 * column;
                luminance = 0.2126f * srgb_to_linear[pixel[2]] +
                            0.7152f * srgb_to_linear[pixel[1]] + 0.0722f * srgb_to_linear[pixel[0]];
            }
            destination[column] =
                static_cast<std::uint8_t>(std::lround(std::clamp(luminance, 0.0f, 1.0f) * 255.0f));
        }
    }
    return linear;
}

vec3f SunDirectionWorld()
{
    return gdt::normalize(vec3f(1, 1, 0.3f));
}

spectra_rt::SSceneInstanceConfig MakeBodyInstance(const SOptions& options)
{
    spectra_rt::SSceneInstanceConfig instance;
    instance.key = "body";
    if (options.scene == "bennu")
        instance.motion.spin.angularVelocity_W_rad_per_s.z =
            static_cast<float>(2.0 * std::numbers::pi_v<double> / bennu_spin_period_s);
    return instance;
}

double PhaseAngleDeg(const vec3f& camera_position_km)
{
    const float distance_km = gdt::length(camera_position_km);
    if (distance_km <= 0.0f)
        throw std::runtime_error("Cannot define phase angle with the camera at the body origin");
    const float cosine = gdt::dot(camera_position_km / distance_km, SunDirectionWorld());
    return std::acos(std::clamp(cosine, -1.0f, 1.0f)) / radians_per_degree;
}

pyramid_klt::SCameraIntrinsics MakeKltCamera(const spectra_rt::SCameraSensorConfig& sensor)
{
    const auto& intrinsics = sensor.camera.intrinsics;
    const double center_x_px = 0.5 * intrinsics.frameWidth_px;
    const double center_y_px = 0.5 * intrinsics.frameHeight_px;
    // Match the centered, zero-skew pinhole rays traced by MakeCameraFromConfig.
    if (intrinsics.model != spectra_rt::SCameraIntrinsics::ECameraModel::Pinhole ||
        intrinsics.skew != 0.0f || !std::isfinite(intrinsics.principalPointX_px) ||
        !std::isfinite(intrinsics.principalPointY_px) ||
        std::abs(intrinsics.principalPointX_px - center_x_px) > 1e-3 ||
        std::abs(intrinsics.principalPointY_px - center_y_px) > 1e-3)
        throw std::invalid_argument("Rendered KLT MSAC requires centered, zero-skew pinhole rays");
    if (!std::isfinite(intrinsics.focalLengthX_px) || !std::isfinite(intrinsics.focalLengthY_px) ||
        intrinsics.focalLengthX_px <= 0.0f || intrinsics.focalLengthY_px <= 0.0f)
        throw std::invalid_argument("Rendered KLT MSAC requires positive finite focal lengths");
    pyramid_klt::SCameraIntrinsics klt_camera;
    klt_camera.image_width = static_cast<std::uint32_t>(intrinsics.frameWidth_px);
    klt_camera.image_height = static_cast<std::uint32_t>(intrinsics.frameHeight_px);
    klt_camera.fx = intrinsics.focalLengthX_px;
    klt_camera.fy = intrinsics.focalLengthY_px;
    klt_camera.cx = center_x_px;
    klt_camera.cy = center_y_px;
    return klt_camera;
}

spectra_rt::CScene MakeScene(const SOptions& options)
{
    using namespace spectra_rt;
    CSceneDefinition definition;
    if (options.albedo_jpeg.empty())
        definition.addMaterial("rock", {SLambertianParams{vec3f(0.05f)}});
    else
    {
        definition.addTexture("linear_albedo", LoadLinearAlbedo(options.albedo_jpeg));
        definition.addMaterial("rock", {SLambertianParams{vec3f(0.05f)}}, "linear_albedo");
    }
    if (options.scene == "sphere")
        definition.addGeometry("body", SSphereDescriptor{{0, 0, 0}, 0.25f});
    else
        definition.addGeometry("body", loadObj(options.model.string()));
    definition.addRenderable("body", "body", "rock");
    definition.addInstance("body", MakeBodyInstance(options));

    // Author the finite solar disk in kilometres; worldUnit_m converts geometry to SI.
    constexpr float sun_distance_km = 149597870.7f;
    const vec3f sun_direction = SunDirectionWorld();
    definition.addAnalyticLight(
        CAnalyticLight::makeSphere(sun_direction * sun_distance_km, 695700.0f, vec3f(1)));
    return CSceneBuilder::buildScene(definition);
}

void VerifyGpuOne()
{
    const char* visible_devices = std::getenv("CUDA_VISIBLE_DEVICES");
    if (!visible_devices || std::string(visible_devices) != "1")
        throw std::runtime_error("Set CUDA_VISIBLE_DEVICES=1 for the physical GPU 1");
    cudaDeviceProp properties{};
    if (cudaGetDeviceProperties(&properties, 0) != cudaSuccess)
        throw std::runtime_error("Cannot inspect logical CUDA device 0");
    const std::string name(properties.name);
    if (name.find("4070 Ti") == std::string::npos)
        throw std::runtime_error("Visible device is not the expected RTX 4070 Ti: " + name);
    std::cout << "CUDA logical 0 = physical 1: " << name << '\n';
}

void Render(const SOptions& options, SCameraControl& control,
            demo::CLatestMailbox<demo::SPreviewFrame>& outgoing, const std::atomic<bool>& stop)
{
    using namespace spectra_rt;
    VerifyGpuOne();
    auto sensor = CRendererConfigParser::ParseCamera(options.camera_yaml.string());
    const cv::Size image_size_px(sensor.camera.intrinsics.frameWidth_px,
                                 sensor.camera.intrinsics.frameHeight_px);
    if (image_size_px != cv::Size(2048, 1536) ||
        sensor.film.readout.mode != ESensorReadout::BAYER_RAW)
        throw std::invalid_argument("The demo requires the 2048x1536 Bayer WFOV profile");
    if (!(sensor.film.fullWellCapacity > 0.0f) || !std::isfinite(sensor.film.fullWellCapacity))
        throw std::invalid_argument("The WFOV full-well reference must be positive and finite");
    std::optional<pyramid_klt::SCameraIntrinsics> klt_camera;
    if (options.mode != demo::EMode::Centroid)
        klt_camera = MakeKltCamera(sensor);

    const auto scene_start = Clock::now();
    auto scene = MakeScene(options);
    CSpectralRayTracer renderer(true);
    const auto traversable = renderer.buildScene(scene);
    const double scene_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - scene_start).count();
    std::cout << "Scene load/build " << scene_ms << " ms\n";

    const auto pose = ReadPose(control);
    sensor.camera.pose.position_W = pose.position_km;
    sensor.camera.pose.lookAtPoint_W = pose.target_km;
    sensor.camera.pose.yDirection_W = {0, 0, -1};
    LaunchParams params;
    params.frame.fbSize = {image_size_px.width, image_size_px.height};
    params.frame.sensorReadout = sensor.film.readout;
    params.camera = ResolveCameraLaunchData(MakeCameraFromConfig(sensor.camera), sensor);
    params.traversable = traversable;
    auto& settings = params.renderSettings;
    settings.radiometryMode = ERadiometryMode::FACTORIZED_SPECTRAL;
    settings.factorizedOutputProduct = EFactorizedOutputProduct::SENSOR_MEASUREMENT;
    settings.factorizedSensorWavebands = EFactorizedSensorWavebands::RGB;
    settings.factorizedLightSourceMeasure =
        EFactorizedLightSourceMeasure::UNIT_SOURCE_RADIANCE_FINITE_AREA;
    settings.factorizedLightSpectrumModel = ESourceSpectrumModel::BLACKBODY;
    settings.factorizedLightBlackbodyTemperature_K = 5778.0f;
    settings.factorizedSourceSpectrumQuantity = ESourceSpectrumQuantity::PHOTON;
    settings.worldUnit_m = 1000.0f;
    settings.spectralWavelengthMin_nm = 440.0f;
    settings.spectralWavelengthMax_nm = 1000.0f;
    settings.outputMode = ERenderOutputMode::FLOAT_ONLY;
    settings.pixelSamplingMode = EPixelSamplingMode::STRATIFIED_MC;
    settings.lightPathWeightingMode = ELightPathWeightingMode::NEE_ZERO_ONE;
    settings.numSampleRaysPerPixel = options.spp;
    settings.numDirectLightSamplesPerLight = 4;
    settings.maxPathDepth = 3;
    settings.useLightIrradianceBypass = 0;
    renderer.setLaunchParams(params, sensor);
    renderer.buildPipelineAndSBT();

    const auto info = renderer.getFloatImageInfo();
    const std::size_t pixels = static_cast<std::size_t>(image_size_px.area());
    std::vector<float> raw_electrons(pixels);
    std::vector<float> grayscale_electrons(pixels);
    cv::Mat processing_gray(image_size_px, CV_8UC1);
    demo::CFrameProcessor processor(image_size_px, options.mode, demo::EKltExtraction::Space,
                                    klt_camera, options.centroid_model);
    demo::CFrameWriter writer(options.output_dir);
    const bool body_spins = options.scene == "bennu" && options.spin_multiplier > 0.0;
    const std::vector<SSceneInstanceConfig> body_instances{MakeBodyInstance(options)};
    const vec3f sun_direction = SunDirectionWorld();
    std::ostringstream metadata;
    metadata << std::setprecision(10) << "{\"schema\":1,\"program\":\"render_stream_demo\""
             << ",\"mode\":" << demo::JsonQuote(demo::ModeName(options.mode))
             << ",\"scene\":" << demo::JsonQuote(options.scene)
             << ",\"view\":" << demo::JsonQuote(options.view)
             << ",\"camera_azimuth_deg\":" << options.camera_azimuth_deg
             << ",\"camera_orbit_step_deg\":" << options.orbit_step_deg << ",\"body_motion\":\""
             << (body_spins ? "nominal_spin" : "static") << '"' << ",\"body_spin_period_s\":";
    if (options.scene == "bennu")
        metadata << bennu_spin_period_s;
    else
        metadata << "null";
    metadata << ",\"body_spin_multiplier\":" << (body_spins ? options.spin_multiplier : 0.0)
             << ",\"body_spin_axis_world\":";
    if (options.scene == "bennu")
        metadata << "[0,0,1]";
    else
        metadata << "null";
    metadata << ",\"sun_motion\":\"fixed_world\"" << ",\"sun_direction_world\":[" << sun_direction.x
             << ',' << sun_direction.y << ',' << sun_direction.z << ']'
             << ",\"model\":" << demo::JsonQuote(options.model.string())
             << ",\"albedo_jpeg\":" << demo::JsonQuote(options.albedo_jpeg.string())
             << ",\"camera_yaml\":" << demo::JsonQuote(options.camera_yaml.string())
             << ",\"klt_outlier_rejection\":" << demo::JsonQuote(klt_camera ? "msac" : "off");
    if (klt_camera)
        metadata << ",\"klt_camera_px\":{\"fx\":" << klt_camera->fx << ",\"fy\":" << klt_camera->fy
                 << ",\"cx\":" << klt_camera->cx << ",\"cy\":" << klt_camera->cy << '}'
                 << ",\"klt_msac_max_distance_px\":" << demo::msac_max_distance_px;
    metadata << ",\"centroid_model\":" << demo::JsonQuote(options.centroid_model.string())
             << ",\"width_px\":" << image_size_px.width << ",\"height_px\":" << image_size_px.height
             << ",\"preview_white_electrons\":" << sensor.film.fullWellCapacity
             << ",\"spp\":" << options.spp << ",\"physical_gpu_index\":1,\"world_unit_m\":1000}";
    writer.writeRunMetadata(metadata.str());
    const auto stream_start = Clock::now();

    for (std::uint64_t index = 0; !stop; ++index)
    {
        if (options.max_frames && index >= options.max_frames)
            break;
        const auto frame_start = Clock::now();
        const double timestamp_s =
            std::chrono::duration<double>(frame_start - stream_start).count();
        if (index > 0 && options.orbit_step_deg != 0.0f)
        {
            std::lock_guard lock(control.mutex);
            control.azimuth_rad += options.orbit_step_deg * 0.017453292519943295f;
        }
        const auto current_pose = ReadPose(control);
        const double phase_angle_deg = PhaseAngleDeg(current_pose.position_km);
        double scene_update_ms = 0.0;
        std::optional<double> body_spin_phase_deg;
        if (body_spins)
        {
            // Reduce to one rotation before converting elapsed time to the renderer's float time.
            const double phase_time_s =
                std::fmod(timestamp_s * options.spin_multiplier, bennu_spin_period_s);
            body_spin_phase_deg = 360.0 * phase_time_s / bennu_spin_period_s;
            const auto update_start = Clock::now();
            renderer.updateSceneInstances(scene_motion::ResolveWorldInstances(
                body_instances, static_cast<float>(phase_time_s)));
            scene_update_ms =
                std::chrono::duration<double, std::milli>(Clock::now() - update_start).count();
        }
        sensor.camera.pose.position_W = current_pose.position_km;
        sensor.camera.pose.lookAtPoint_W = current_pose.target_km;
        renderer.setCameraLaunchData(
            ResolveCameraLaunchData(MakeCameraFromConfig(sensor.camera), sensor));
        renderer.setFrameID(static_cast<std::uint32_t>(index));
        const auto render_start = Clock::now();
        renderer.renderFrame();
        const auto render_done = Clock::now();
        renderer.getSensorRaw(raw_electrons);
        const auto readback_done = Clock::now();
        radiometry::ReconstructBayerImage(info, raw_electrons, {}, grayscale_electrons);

        // Keep raw expected electrons unchanged; use one full-well display scale for the run.
        const float byte_per_electron = 255.0f / sensor.film.fullWellCapacity;
        for (std::size_t pixel = 0; pixel < pixels; ++pixel)
            processing_gray.data[pixel] = static_cast<std::uint8_t>(
                std::clamp(grayscale_electrons[pixel] * byte_per_electron, 0.0f, 255.0f));
        const auto reconstruction_done = Clock::now();
        const double render_ms =
            std::chrono::duration<double, std::milli>(render_done - render_start).count();
        const double readback_ms =
            std::chrono::duration<double, std::milli>(readback_done - render_done).count();
        const double reconstruction_ms =
            std::chrono::duration<double, std::milli>(reconstruction_done - readback_done).count();
        const double source_ms =
            std::chrono::duration<double, std::milli>(reconstruction_done - frame_start).count();
        auto preview = processor.process({processing_gray, index, timestamp_s, source_ms, render_ms,
                                          readback_ms, reconstruction_ms, scene_update_ms,
                                          phase_angle_deg, body_spin_phase_deg},
                                         index, outgoing.dropped());
        std::cout << preview.summary.line() << " | mask " << preview.summary.mask_status
                  << " | IAS " << scene_update_ms << " ms | source " << source_ms
                  << " ms | perception " << preview.summary.processing_ms << " ms\n";
        writer.write(preview);
        outgoing.publish(std::move(preview));
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto options = ParseOptions(argc, argv);
        std::unique_ptr<demo::CPreviewWindow> window;
        if (!options.headless)
            window = std::make_unique<demo::CPreviewWindow>("Spectra-RT perception stream",
                                                            cv::Size{1200, 900});
        SCameraControl control;
        ResetView(control, options);
        demo::CLatestMailbox<demo::SPreviewFrame> outgoing;
        std::atomic<bool> stop{false};
        std::exception_ptr failure;
        double preview_total_ms = 0.0;
        std::uint64_t presented_frames = 0;
        std::thread worker(
            [&]
            {
                try
                {
                    Render(options, control, outgoing, stop);
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                outgoing.close();
            });

        if (options.headless)
        {
            while (!outgoing.closedAndEmpty())
            {
                (void)outgoing.tryTake();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        else
        {
            auto* handle = window->nativeHandle();
            glfwSetWindowUserPointer(handle, &control);
            glfwSetMouseButtonCallback(handle, MouseButton);
            glfwSetCursorPosCallback(handle, CursorMove);
            glfwSetScrollCallback(handle, Scroll);
            while (!window->shouldClose() && !outgoing.closedAndEmpty())
            {
                window->pollEvents();
                PollKeyboard(handle, control, options);
                if (auto preview = outgoing.tryTake())
                {
                    preview_total_ms += window->show(preview->bgr);
                    ++presented_frames;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            stop = true;
        }
        worker.join();
        if (presented_frames)
            std::cout << "Preview: " << presented_frames << " frames, mean "
                      << preview_total_ms / presented_frames << " ms per upload/draw/swap\n";
        if (failure)
            std::rethrow_exception(failure);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "render_stream_demo: " << error.what() << '\n';
        return 1;
    }
}
