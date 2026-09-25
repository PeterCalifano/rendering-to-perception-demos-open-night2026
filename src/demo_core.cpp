/**
 * @file demo_core.cpp
 * @brief Process frames with space-aware KLT and present aligned diagnostics.
 */
#include "demo_core.h"
#ifdef DEMO_ENABLE_ML
#include "model_adapter.h"
#endif

#include <pyramidal_klt/utils/logging/CLogger.h>

#include <GLFW/glfw3.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace demo
{
namespace
{
using Clock = std::chrono::steady_clock;

std::string MaskName(pyramid_klt::EIlluminatedBodyMaskStatus status)
{
    using Status = pyramid_klt::EIlluminatedBodyMaskStatus;
    switch (status)
    {
    case Status::Disabled:
        return "OFF";
    case Status::NotEvaluated:
        return "WAIT";
    case Status::Ready:
        return "READY";
    case Status::EmptyForeground:
        return "EMPTY";
    }
    return "UNKNOWN";
}

std::string MsacName(pyramid_klt::EMsacStatus status)
{
    using Status = pyramid_klt::EMsacStatus;
    switch (status)
    {
    case Status::Disabled:
        return "OFF";
    case Status::InsufficientCorrespondences:
        return "WAIT";
    case Status::Valid:
        return "VALID";
    case Status::ModelFailure:
        return "FAIL";
    }
    return "UNKNOWN";
}

void DrawSummary(cv::Mat& image, const SFrameSummary& summary)
{
    const bool compact = image.cols < 1000;
    const int band_height_px = compact ? 86 : 72;
    cv::rectangle(image, {0, 0, image.cols, std::min(image.rows, band_height_px)},
                  cv::Scalar(18, 24, 32), cv::FILLED);

    const cv::Scalar text_color(235, 239, 245);
    const auto draw_line = [&image](const std::string& line, int baseline_px,
                                    const cv::Scalar& color, double preferred_scale)
    {
        double scale = preferred_scale;
        int baseline = 0;
        while (scale > 0.25 &&
               cv::getTextSize(line, cv::FONT_HERSHEY_SIMPLEX, scale, 1, &baseline).width >
                   image.cols - 25)
            scale -= 0.02;
        cv::putText(image, line, {15, baseline_px}, cv::FONT_HERSHEY_SIMPLEX, scale, color, 1,
                    cv::LINE_AA);
    };

    std::ostringstream timing;
    timing << std::fixed << std::setprecision(1) << "source " << summary.source_ms << " ms";
    if (summary.phase_angle_deg)
        timing << "  IAS " << summary.scene_update_ms << " ms";
    timing << "  KLT " << summary.klt_ms << " ms  centroid " << summary.centroid_ms << " ms  YOLO "
           << summary.yolo_ms << " ms  dropped " << summary.dropped_frames;
    if (compact)
    {
        std::ostringstream first;
        first << "frame " << summary.processed_index << "  src " << summary.source_index << "  t "
              << std::fixed << std::setprecision(2) << summary.timestamp_s << " s";
        if (summary.klt_enabled)
            first << "  KLT " << summary.active_features << "/" << summary.tracked_features << "  +"
                  << summary.new_features << "  -" << summary.lost_features;
        else
            first << "  KLT OFF";

        std::ostringstream second;
        second << "mask " << summary.mask_status << "  eligible " << std::fixed
               << std::setprecision(1) << summary.eligible_fraction * 100.0 << "%" << "  retry "
               << (summary.extraction_retry ? "YES" : "NO") << "  centroid "
               << summary.centroid_status;
        if (summary.centroid_px)
            second << " (" << std::setprecision(0) << summary.centroid_px->x << ","
                   << summary.centroid_px->y << ")";
        if (summary.yolo_enabled)
            second << "  YOLO " << summary.yolo_detections;
        if (summary.msac_status != "OFF")
            second << "  MSAC " << summary.msac_status << " -" << summary.msac_outliers;
        draw_line(first.str(), 22, text_color, 0.53);
        draw_line(second.str(), 47, text_color, 0.49);
        draw_line(timing.str(), 72, cv::Scalar(171, 206, 226), 0.48);
    }
    else
    {
        std::ostringstream second;
        second << "mask " << summary.mask_status << "  eligible " << std::fixed
               << std::setprecision(1) << summary.eligible_fraction * 100.0 << "%" << "  retry "
               << (summary.extraction_retry ? "YES" : "NO") << "  " << timing.str();
        draw_line(summary.line(), 28, text_color, 0.62);
        draw_line(second.str(), 56, cv::Scalar(171, 206, 226), 0.55);
    }
}
} // namespace

std::string SFrameSummary::line() const
{
    std::ostringstream out;
    out << "frame " << processed_index << "  src " << source_index << "  t " << std::fixed
        << std::setprecision(3) << timestamp_s << " s";
    if (klt_enabled)
        out << "  KLT " << active_features << " active / " << tracked_features << " tracked / +"
            << new_features << " / -" << lost_features;
    else
        out << "  KLT OFF";
    out << "  centroid " << centroid_status;
    if (centroid_px)
    {
        out << " (" << std::setprecision(1) << centroid_px->x << "," << centroid_px->y << ")";
    }
    if (yolo_enabled)
        out << "  YOLO " << yolo_detections;
    if (msac_status != "OFF")
        out << "  MSAC " << msac_status << " -" << msac_outliers;
    if (phase_angle_deg)
        out << "  phase " << std::setprecision(1) << *phase_angle_deg << " deg";
    if (body_spin_phase_deg)
        out << "  spin " << std::setprecision(1) << *body_spin_phase_deg << " deg";
    return out.str();
}

std::string SFrameSummary::timingLine() const
{
    std::ostringstream out;
    out << "frame " << processed_index << " stages [ms]: " << std::fixed << std::setprecision(1);
    if (phase_angle_deg)
    {
        out << "scene update " << scene_update_ms << " | render " << render_ms << " | readback "
            << readback_ms << " | reconstruct " << reconstruction_ms << " | source " << source_ms;
    }
    else
    {
        out << "capture " << source_ms;
    }
    if (klt_enabled)
        out << " | KLT " << klt_ms;
    if (centroid_status != "OFF")
        out << " | centroid " << centroid_ms;
    if (yolo_enabled)
        out << " | YOLO " << yolo_ms;
    out << " | processing " << processing_ms;
    return out.str();
}

void LogFrameSummary(const SFrameSummary& summary)
{
    using pyramidal_klt::logging::CLogger;
    using pyramidal_klt::logging::ELogLevel;
    static CLogger logger("perception-demo", ELogLevel::Info);
    static const bool configured = logger.setLevelFromEnvironment("DEMO_LOG_LEVEL");
    (void)configured;

    logger.info(summary.line(), " | mask ", summary.mask_status, " | dropped ",
                summary.dropped_frames);
    logger.info(summary.timingLine());
}

std::string SFrameSummary::json() const
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << "{\"source_index\":" << source_index
        << ",\"processed_index\":" << processed_index << ",\"timestamp_s\":" << timestamp_s
        << ",\"klt_enabled\":" << (klt_enabled ? "true" : "false")
        << ",\"active_features\":" << active_features
        << ",\"tracked_features\":" << tracked_features << ",\"new_features\":" << new_features
        << ",\"lost_features\":" << lost_features << ",\"mask_status\":\"" << mask_status << "\""
        << ",\"eligible_fraction\":" << eligible_fraction
        << ",\"extraction_retry\":" << (extraction_retry ? "true" : "false")
        << ",\"msac_status\":\"" << msac_status << "\",\"msac_outliers\":" << msac_outliers
        << ",\"centroid_status\":\"" << centroid_status << "\"" << ",\"source_ms\":" << source_ms
        << ",\"render_ms\":" << render_ms << ",\"readback_ms\":" << readback_ms
        << ",\"reconstruction_ms\":" << reconstruction_ms
        << ",\"scene_update_ms\":" << scene_update_ms << ",\"klt_ms\":" << klt_ms
        << ",\"centroid_ms\":" << centroid_ms << ",\"yolo_ms\":" << yolo_ms
        << ",\"processing_ms\":" << processing_ms << ",\"dropped_frames\":" << dropped_frames;
    if (centroid_px)
    {
        out << ",\"centroid_px\":[" << centroid_px->x << ',' << centroid_px->y << ']';
    }
    if (phase_angle_deg)
        out << ",\"phase_angle_deg\":" << *phase_angle_deg;
    if (body_spin_phase_deg)
        out << ",\"body_spin_phase_deg\":" << *body_spin_phase_deg;
    out << ",\"yolo_detections\":" << yolo_detections << ",\"active_track_ids\":[";
    for (std::size_t index = 0; index < active_track_ids.size(); ++index)
    {
        if (index)
            out << ',';
        out << active_track_ids[index];
    }
    out << "]}";
    return out.str();
}

CFrameProcessor::CFrameProcessor(cv::Size image_size_px, EMode mode, EKltExtraction extraction,
                                 std::optional<pyramid_klt::SCameraIntrinsics> camera_intrinsics,
                                 const std::filesystem::path& centroid_model,
                                 const std::filesystem::path& yolo_model)
    : mode_(mode)
{
    if (image_size_px.width <= 0 || image_size_px.height <= 0)
    {
        throw std::invalid_argument("Processor image size must be positive");
    }
    if (mode_ != EMode::Centroid)
    {
        pyramid_klt::SKltPipelineSettings settings;
        settings.camera.image_width = static_cast<std::uint32_t>(image_size_px.width);
        settings.camera.image_height = static_cast<std::uint32_t>(image_size_px.height);
        if (camera_intrinsics)
        {
            if (camera_intrinsics->image_width != settings.camera.image_width ||
                camera_intrinsics->image_height != settings.camera.image_height)
                throw std::invalid_argument("KLT calibration must match the input image size");
            settings.camera = *camera_intrinsics;
            settings.tracker_settings.outlier_rejection_flag = true;
            // Allow one pixel of localization error before rejecting epipolar outliers.
            settings.tracker_settings.msac_max_distance = msac_max_distance_px;
        }
        settings.detector.selection_policy = pyramid_klt::EFeatureSelectionPolicy::KmeansCoverage;
        settings.illumination_mask.enabled = extraction == EKltExtraction::Space;
        klt_.emplace(settings);
    }
#ifdef DEMO_ENABLE_ML
    if (mode_ != EMode::Klt && centroid_model.empty())
        throw std::invalid_argument("Centroid and both modes require --centroid-model");
    if (!centroid_model.empty() || !yolo_model.empty())
        models_ = std::make_unique<CModelAdapter>(centroid_model, yolo_model);
#else
    if (mode_ != EMode::Klt || !centroid_model.empty() || !yolo_model.empty())
        throw std::invalid_argument("Models require DEMO_ENABLE_ML=ON");
#endif
}

CFrameProcessor::~CFrameProcessor() = default;

SPreviewFrame CFrameProcessor::process(const SSourceFrame& frame, std::uint64_t processed_index,
                                       std::uint64_t dropped_frames)
{
    if (frame.image.empty())
        throw std::invalid_argument("Cannot process an empty frame");

    const auto start = Clock::now();
    SPreviewFrame preview;
    if (frame.image.type() == CV_8UC3)
    {
        preview.bgr = frame.image.clone();
    }
    else if (frame.image.type() == CV_8UC1)
    {
        cv::cvtColor(frame.image, preview.bgr, cv::COLOR_GRAY2BGR);
    }
    else
    {
        throw std::invalid_argument("Frames must be grayscale8 or BGR8");
    }

    auto& summary = preview.summary;
    summary.source_index = frame.source_index;
    summary.processed_index = processed_index;
    summary.timestamp_s = frame.timestamp_s;
    summary.source_ms = frame.acquisition_ms;
    summary.render_ms = frame.render_ms;
    summary.readback_ms = frame.readback_ms;
    summary.reconstruction_ms = frame.reconstruction_ms;
    summary.scene_update_ms = frame.scene_update_ms;
    summary.phase_angle_deg = frame.phase_angle_deg;
    summary.body_spin_phase_deg = frame.body_spin_phase_deg;
    summary.dropped_frames = dropped_frames;

    cv::Mat grayscale;
    if (frame.image.channels() == 3)
        cv::cvtColor(frame.image, grayscale, cv::COLOR_BGR2GRAY);
    else
        grayscale = frame.image;

    if (klt_)
    {
        summary.klt_enabled = true;
        const auto klt_start = Clock::now();
        const auto result = klt_->step(grayscale);
        summary.active_features = result.tracked_features.size();
        summary.tracked_features = result.num_tracked_features;
        summary.new_features = result.num_detected_features;
        summary.lost_features = result.num_lost_features;
        summary.mask_status = MaskName(result.illumination_mask_status);
        summary.eligible_fraction = result.illumination_eligible_fraction;
        summary.extraction_retry = result.extraction_retry_pending;
        summary.msac_status = MsacName(result.msac_status);
        summary.msac_outliers = result.msac_outlier_input_indices.size();

        // Cache display positions only; KLT owns the IDs and retires lost tracks.
        std::unordered_map<std::uint64_t, std::deque<cv::Point2f>> active_trails_px;
        active_trails_px.reserve(result.active_feature_ids.size());
        summary.active_track_ids.reserve(result.active_feature_ids.size());
        for (std::size_t index = 0; index < result.active_feature_ids.size(); ++index)
        {
            const auto id = result.active_feature_ids[index].value();
            summary.active_track_ids.push_back(id);
            const auto& feature = result.tracked_features[index];
            const cv::Point2f point_px(static_cast<float>(feature.u),
                                       static_cast<float>(feature.v));
            auto previous = trails_px_.find(id);
            auto& trail = active_trails_px[id];
            if (previous != trails_px_.end())
                trail = std::move(previous->second);
            trail.push_back(point_px);
            if (trail.size() > 8)
                trail.pop_front();

            // Color oldest positions red and the current position yellow.
            for (std::size_t step = 0; step < trail.size(); ++step)
            {
                const float progress =
                    static_cast<float>(step) / std::max<std::size_t>(1, trail.size() - 1);
                const cv::Scalar color(40, 55 + 175 * progress, 210 + 43 * progress);
                if (step)
                    cv::line(preview.bgr, trail[step - 1], trail[step], color, 2, cv::LINE_AA);
                cv::circle(preview.bgr, trail[step], step + 1 == trail.size() ? 3 : 2, color, -1,
                           cv::LINE_AA);
            }
        }
        trails_px_ = std::move(active_trails_px);
        summary.klt_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - klt_start).count();
    }

#ifdef DEMO_ENABLE_ML
    if (models_ && models_->hasCentroid())
    {
        const auto centroid_start = Clock::now();
        try
        {
            summary.centroid_px = models_->centroid(grayscale);
            const auto point_px = *summary.centroid_px;
            const bool inside = point_px.x >= 0 && point_px.y >= 0 &&
                                point_px.x < frame.image.cols && point_px.y < frame.image.rows;
            summary.centroid_status = inside ? "OK" : "OUTSIDE";
            if (inside)
            {
                const cv::Point point(static_cast<int>(std::round(point_px.x)),
                                      static_cast<int>(std::round(point_px.y)));
                cv::drawMarker(preview.bgr, point, cv::Scalar(255, 60, 200), cv::MARKER_CROSS, 30,
                               3, cv::LINE_AA);
            }
        }
        catch (const std::exception& error)
        {
            summary.centroid_status = "ERROR";
            std::cerr << "Centroid failed for source frame " << frame.source_index << ": "
                      << error.what() << '\n';
        }
        summary.centroid_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - centroid_start).count();
    }
    if (models_ && models_->hasYolo())
    {
        const auto yolo_start = Clock::now();
        summary.yolo_enabled = true;
        cv::Mat source_bgr;
        if (frame.image.channels() == 3)
            source_bgr = frame.image;
        else
            cv::cvtColor(frame.image, source_bgr, cv::COLOR_GRAY2BGR);
        const auto boxes = models_->detections(source_bgr);
        summary.yolo_detections = boxes.size();
        for (const auto& box : boxes)
        {
            cv::rectangle(preview.bgr, box.bounds_px, cv::Scalar(90, 235, 92), 2, cv::LINE_AA);
        }
        summary.yolo_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - yolo_start).count();
    }
#endif

    summary.processing_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    DrawSummary(preview.bgr, summary);
    return preview;
}

CFrameWriter::CFrameWriter(const std::filesystem::path& output_dir) : output_dir_(output_dir)
{
    if (output_dir_.empty())
        return;
    std::filesystem::create_directories(output_dir_ / "frames");
    jsonl_ = std::make_unique<std::ofstream>(output_dir_ / "frames.jsonl");
    if (!*jsonl_)
        throw std::runtime_error("Cannot open frames.jsonl");
}

void CFrameWriter::writeRunMetadata(const std::string& json_object)
{
    if (!jsonl_)
        return;
    std::ofstream output(output_dir_ / "run.json");
    output << json_object << '\n';
    if (!output)
        throw std::runtime_error("Cannot write run.json");
}

void CFrameWriter::write(const SPreviewFrame& frame)
{
    if (!jsonl_)
        return;
    std::ostringstream name;
    name << "frame_" << std::setw(6) << std::setfill('0') << frame.summary.processed_index
         << ".png";
    if (!cv::imwrite((output_dir_ / "frames" / name.str()).string(), frame.bgr))
        throw std::runtime_error("Cannot save annotated frame");
    *jsonl_ << frame.summary.json() << '\n';
    jsonl_->flush();
}

CPreviewWindow::CPreviewWindow(const char* title, cv::Size initial_size_px)
{
    if (!glfwInit())
        throw std::runtime_error("Cannot initialize GLFW");
    window_ =
        glfwCreateWindow(initial_size_px.width, initial_size_px.height, title, nullptr, nullptr);
    if (!window_)
    {
        glfwTerminate();
        throw std::runtime_error("Cannot create GLFW window");
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

CPreviewWindow::~CPreviewWindow()
{
    if (window_)
    {
        glfwMakeContextCurrent(window_);
        glDeleteTextures(1, &texture_);
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool CPreviewWindow::shouldClose() const
{
    return glfwWindowShouldClose(window_);
}

void CPreviewWindow::pollEvents()
{
    glfwPollEvents();
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

double CPreviewWindow::show(const cv::Mat& bgr)
{
    if (bgr.type() != CV_8UC3 || !bgr.isContinuous())
        throw std::invalid_argument("Preview needs contiguous BGR8");
    const auto start = Clock::now();

    int width_px = 0;
    int height_px = 0;
    glfwGetFramebufferSize(window_, &width_px, &height_px);
    glViewport(0, 0, width_px, height_px);
    glClearColor(0.03f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texture_size_px_ != bgr.size())
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, bgr.cols, bgr.rows, 0, GL_BGR, GL_UNSIGNED_BYTE,
                     bgr.data);
        texture_size_px_ = bgr.size();
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bgr.cols, bgr.rows, GL_BGR, GL_UNSIGNED_BYTE,
                        bgr.data);
    }

    // Preserve the detector aspect ratio as the window changes size.
    const float image_aspect = static_cast<float>(bgr.cols) / bgr.rows;
    const float window_aspect = static_cast<float>(width_px) / std::max(1, height_px);
    const float half_width = window_aspect > image_aspect ? image_aspect / window_aspect : 1.0f;
    const float half_height = window_aspect > image_aspect ? 1.0f : window_aspect / image_aspect;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1);
    glVertex2f(-half_width, half_height);
    glTexCoord2f(1, 1);
    glVertex2f(half_width, half_height);
    glTexCoord2f(1, 0);
    glVertex2f(half_width, -half_height);
    glTexCoord2f(0, 0);
    glVertex2f(-half_width, -half_height);
    glEnd();
    glfwSwapBuffers(window_);
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::uint32_t ParsePositiveCount(const std::string& value, const char* option)
{
    std::uint32_t count = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || count == 0)
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    return count;
}

std::string JsonQuote(std::string_view value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char byte : value)
    {
        if (byte == '"' || byte == '\\')
            out << '\\' << static_cast<char>(byte);
        else if (byte == '\n')
            out << "\\n";
        else if (byte == '\r')
            out << "\\r";
        else if (byte == '\t')
            out << "\\t";
        else if (byte < 0x20)
            out << '?';
        else
            out << static_cast<char>(byte);
    }
    out << '"';
    return out.str();
}

std::string_view ModeName(EMode mode)
{
    switch (mode)
    {
    case EMode::Klt:
        return "klt";
    case EMode::Centroid:
        return "centroid";
    case EMode::Both:
        return "both";
    }
    return "unknown";
}

std::string_view ExtractionName(EKltExtraction extraction)
{
    return extraction == EKltExtraction::Space ? "space" : "generic";
}
} // namespace demo
