/**
 * @file demo_core.h
 * @brief Shared frame processing, summaries, and preview contracts for both demos.
 */
#pragma once

#include <klt_pipeline/CFrontendKltPipeline.h>
#include <opencv2/core/mat.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace demo
{

/** Pixel-domain tolerance for calibrated KLT geometric rejection. */
inline constexpr double msac_max_distance_px = 1.0;

class CModelAdapter;

enum class EMode
{
    Klt,
    Centroid,
    Both
};
enum class EKltExtraction
{
    Space,
    Generic
};

/** A source image and its source-owned identity. Images are BGR8 or grayscale8. */
struct SSourceFrame
{
    cv::Mat image;
    std::uint64_t source_index{0};
    double timestamp_s{0.0};
    double acquisition_ms{0.0};
    double render_ms{0.0};
    double readback_ms{0.0};
    double reconstruction_ms{0.0};
    double scene_update_ms{0.0};               ///< Renderer instance-update time, when applicable.
    std::optional<double> phase_angle_deg;     ///< Sun-body-camera angle at the body origin.
    std::optional<double> body_spin_phase_deg; ///< Body rotation from the run's time origin.
};

/** Per-frame measurements used by the console, preview, and JSONL output. */
struct SFrameSummary
{
    std::uint64_t source_index{0};
    std::uint64_t processed_index{0};
    std::uint64_t dropped_frames{0};
    double timestamp_s{0.0};
    std::size_t active_features{0};
    std::uint32_t tracked_features{0};
    std::uint32_t new_features{0};
    std::uint32_t lost_features{0};
    double eligible_fraction{0.0};
    double processing_ms{0.0};
    double source_ms{0.0};
    double render_ms{0.0};
    double readback_ms{0.0};
    double reconstruction_ms{0.0};
    double scene_update_ms{0.0};
    double klt_ms{0.0};
    double centroid_ms{0.0};
    double yolo_ms{0.0};
    bool klt_enabled{false};
    std::string mask_status{"OFF"};
    bool extraction_retry{false};
    std::string msac_status{"OFF"};
    std::size_t msac_outliers{0}; ///< Tracks retired by an accepted geometric model.
    std::string centroid_status{"OFF"};
    std::optional<cv::Point2d> centroid_px;
    bool yolo_enabled{false};
    std::size_t yolo_detections{0};
    std::optional<double> phase_angle_deg;     ///< Sun-body-camera angle, if rendered.
    std::optional<double> body_spin_phase_deg; ///< Body rotation phase, if spinning.
    std::vector<std::uint64_t> active_track_ids;

    /** Format one compact diagnostics line for terminal and preview. */
    [[nodiscard]] std::string line() const;
    /** Format the same record as a standalone JSON object. */
    [[nodiscard]] std::string json() const;
};

/** Fully processed frame; the image already contains its colored overlay. */
struct SPreviewFrame
{
    cv::Mat bgr;
    SFrameSummary summary;
};

/** One-slot mailbox that drops superseded frames while keeping source IDs. */
template <typename T> class CLatestMailbox
{
  public:
    /** Replace any pending item, incrementing the drop count when one is replaced. */
    void publish(T item)
    {
        std::lock_guard lock(mutex_);
        if (item_)
            ++dropped_;
        item_ = std::move(item);
        condition_.notify_one();
    }

    /** Wait for one item or closed input; return empty only after close and drain. */
    [[nodiscard]] std::optional<T> take()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return item_.has_value() || closed_; });
        if (!item_)
            return std::nullopt;
        std::optional<T> item = std::move(item_);
        item_.reset();
        return item;
    }

    /** Retrieve a pending item without blocking, if present. */
    [[nodiscard]] std::optional<T> tryTake()
    {
        std::lock_guard lock(mutex_);
        std::optional<T> item = std::move(item_);
        item_.reset();
        return item;
    }

    /** Wake consumers when the producer has no more frames. */
    void close()
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
        condition_.notify_all();
    }

    [[nodiscard]] std::uint64_t dropped() const
    {
        std::lock_guard lock(mutex_);
        return dropped_;
    }

    [[nodiscard]] bool closedAndEmpty() const
    {
        std::lock_guard lock(mutex_);
        return closed_ && !item_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<T> item_;
    std::uint64_t dropped_{0};
    bool closed_{false};
};

/** Own the stateful KLT pipeline and bounded trails for surviving track IDs. */
class CFrameProcessor
{
  public:
    /**
     * @brief Own KLT and optional model inference for one fixed-resolution stream.
     * @param image_size_px Source image dimensions.
     * @param mode Enabled KLT/centroid pair.
     * @param extraction KLT feature-eligibility policy.
     * @param camera_intrinsics Matching pinhole calibration; enables MSAC when present.
     * @param centroid_model Optional centroid model path.
     * @param yolo_model Optional YOLOv7 model path.
     */
    CFrameProcessor(cv::Size image_size_px, EMode mode, EKltExtraction extraction,
                    std::optional<pyramid_klt::SCameraIntrinsics> camera_intrinsics = std::nullopt,
                    const std::filesystem::path& centroid_model = {},
                    const std::filesystem::path& yolo_model = {});
    ~CFrameProcessor();

    /** Process one frame and draw tracks and a compact summary on a copy. */
    SPreviewFrame process(const SSourceFrame& frame, std::uint64_t processed_index,
                          std::uint64_t dropped_frames);

  private:
    EMode mode_;
    std::optional<pyramid_klt::CFrontendKltPipeline> klt_;
    std::unordered_map<std::uint64_t, std::deque<cv::Point2f>> trails_px_;
#ifdef DEMO_ENABLE_ML
    std::unique_ptr<CModelAdapter> models_;
#endif
};

/** Own an optional output directory and write only fully processed frames. */
class CFrameWriter
{
  public:
    explicit CFrameWriter(const std::filesystem::path& output_dir);
    void writeRunMetadata(const std::string& json_object);
    void write(const SPreviewFrame& frame);

  private:
    std::filesystem::path output_dir_;
    std::unique_ptr<std::ofstream> jsonl_;
};

/** Main-thread GLFW preview; Esc closes it and the caller controls worker lifetime. */
class CPreviewWindow
{
  public:
    CPreviewWindow(const char* title, cv::Size initial_size_px);
    ~CPreviewWindow();
    CPreviewWindow(const CPreviewWindow&) = delete;
    CPreviewWindow& operator=(const CPreviewWindow&) = delete;

    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] GLFWwindow* nativeHandle() const
    {
        return window_;
    }
    void pollEvents();
    /** Upload and present one image; return elapsed upload, draw, and swap time in ms. */
    double show(const cv::Mat& bgr);

  private:
    GLFWwindow* window_{nullptr};
    unsigned int texture_{0};
    cv::Size texture_size_px_;
};

/** Parse a required positive integral CLI value without accepting suffix text. */
std::uint32_t ParsePositiveCount(const std::string& value, const char* option);

/** Quote and escape a string value for a JSON document. */
std::string JsonQuote(std::string_view value);
std::string_view ModeName(EMode mode);
std::string_view ExtractionName(EKltExtraction extraction);

} // namespace demo
