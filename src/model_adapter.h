/**
 * @file model_adapter.h
 * @brief AutoForge inference boundary for centroid and YOLOv7 stream overlays.
 */
#pragma once

#include <opencv2/core/mat.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace demo
{
/** Detection mapped from model pixels to source image pixels. */
struct SDetectionBox
{
    cv::Rect2d bounds_px;
    std::int64_t class_id{0};
    float score{0};
};

/** Load optional semantic models once and run them on the current source image. */
class CModelAdapter
{
  public:
    CModelAdapter(const std::filesystem::path& centroid_model,
                  const std::filesystem::path& yolo_model);
    ~CModelAdapter();

    [[nodiscard]] bool hasCentroid() const;
    [[nodiscard]] bool hasYolo() const;

    /** Return an unclamped source pixel coordinate; empty means no centroid model. */
    [[nodiscard]] std::optional<cv::Point2d> centroid(const cv::Mat& grayscale);
    /** Decode, suppress, and restore detections to source pixel coordinates. */
    [[nodiscard]] std::vector<SDetectionBox> detections(const cv::Mat& bgr);

  private:
    class CImpl;
    std::unique_ptr<CImpl> impl_;
};
} // namespace demo
