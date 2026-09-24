/**
 * @file model_adapter.cpp
 * @brief Keep ONNX tensor policy and postprocessing outside the streaming loops.
 */
#include "model_adapter.h"

#include <inference/model_facade.h>
#include <inference/task_adapters.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace demo
{
namespace
{
namespace infer = ptafdeploy::inference;

cv::Size InputSize(const infer::STensorInfo& info, int channels)
{
    if (info.dtype != "float32" || info.shape.size() != 4 ||
        (info.shape[0] != 1 && info.shape[0] != -1) || info.shape[1] != channels ||
        info.shape[2] <= 0 || info.shape[3] <= 0)
        throw std::invalid_argument("Model input must be concrete NCHW float32");
    return {static_cast<int>(info.shape[3]), static_cast<int>(info.shape[2])};
}

float IntersectionOverUnion(const cv::Rect2d& a, const cv::Rect2d& b)
{
    const double overlap = (a & b).area();
    const double united = a.area() + b.area() - overlap;
    return united > 0 ? static_cast<float>(overlap / united) : 0.0f;
}
} // namespace

class CModelAdapter::CImpl
{
  public:
    std::unique_ptr<infer::CModelFacade> centroid;
    std::unique_ptr<infer::CModelFacade> yolo;
};

CModelAdapter::CModelAdapter(const std::filesystem::path& centroid_model,
                             const std::filesystem::path& yolo_model)
    : impl_(std::make_unique<CImpl>())
{
    if (!centroid_model.empty())
    {
        impl_->centroid = std::make_unique<infer::CModelFacade>();
        infer::SRuntimeConfig runtime;
        runtime.ClearExecutionTargetPriority();
        runtime.AddExecutionTarget(infer::EExecutionTarget::cpu);
        runtime.SetAllowFallback(false);
        runtime.SetLogId("stream_centroid");
        impl_->centroid->LoadModelWithRoleAndRuntimeConfig(centroid_model.string(),
                                                           infer::EModelRole::centroiding, runtime);
        if (impl_->centroid->GetNumInputs() != 1 || impl_->centroid->GetNumOutputs() != 1)
            throw std::invalid_argument("Centroid model must have one input and one output");
        (void)InputSize(impl_->centroid->GetInputInfo(0), 1);
        std::cout << "Centroid backend: " << impl_->centroid->GetBackendDetail() << '\n';
    }
    if (!yolo_model.empty())
    {
        impl_->yolo = std::make_unique<infer::CModelFacade>();
        impl_->yolo->LoadModelConfig(yolo_model.string());
        if (impl_->yolo->GetRole() != "object_detection" || impl_->yolo->GetNumInputs() != 1 ||
            impl_->yolo->GetNumOutputs() != 1)
            throw std::invalid_argument("YOLO manifest must describe one-input object detection");
        (void)InputSize(impl_->yolo->GetInputInfo(0), 3);
        std::cout << "YOLO backend: " << impl_->yolo->GetBackendDetail() << '\n';
    }
}

CModelAdapter::~CModelAdapter() = default;

bool CModelAdapter::hasCentroid() const
{
    return static_cast<bool>(impl_->centroid);
}
bool CModelAdapter::hasYolo() const
{
    return static_cast<bool>(impl_->yolo);
}

std::optional<cv::Point2d> CModelAdapter::centroid(const cv::Mat& grayscale)
{
    if (!impl_->centroid)
        return std::nullopt;
    if (grayscale.type() != CV_8UC1)
        throw std::invalid_argument("Centroid model needs grayscale8");

    const auto input_info = impl_->centroid->GetInputInfo(0);
    const auto size_px = InputSize(input_info, 1);
    cv::Mat resized;
    if (grayscale.size() == size_px)
        resized = grayscale;
    else
        cv::resize(grayscale, resized, size_px, 0, 0, cv::INTER_LINEAR);
    if (!resized.isContinuous())
        resized = resized.clone();
    const auto* pixels = resized.ptr<std::uint8_t>();
    const auto input = infer::MakeNchwFloatTensorFromHwcAccessor(
        input_info.name, static_cast<std::size_t>(size_px.height),
        static_cast<std::size_t>(size_px.width), 1, 1.0f / 255.0f, false,
        [pixels](std::size_t index) { return pixels[index]; });
    const auto output = impl_->centroid->InferSingleFloatTensor(input);
    if (output.shape != std::vector<std::int64_t>{1, 2} || output.values.size() != 2 ||
        !std::isfinite(output.values[0]) || !std::isfinite(output.values[1]))
        throw std::runtime_error("Centroid output must be finite [1,2]");
    return cv::Point2d(output.values[0] * grayscale.cols, output.values[1] * grayscale.rows);
}

std::vector<SDetectionBox> CModelAdapter::detections(const cv::Mat& bgr)
{
    if (!impl_->yolo)
        return {};
    if (bgr.type() != CV_8UC3)
        throw std::invalid_argument("YOLO needs BGR8");

    const auto input_info = impl_->yolo->GetInputInfo(0);
    const auto size_px = InputSize(input_info, 3);
    cv::Mat resized;
    cv::resize(bgr, resized, size_px, 0, 0, cv::INTER_LINEAR);
    if (!resized.isContinuous())
        resized = resized.clone();
    const auto* pixels = resized.ptr<std::uint8_t>();
    const auto input = infer::MakeNchwFloatTensorFromHwcAccessor(
        input_info.name, static_cast<std::size_t>(size_px.height),
        static_cast<std::size_t>(size_px.width), 3, 1.0f / 255.0f, true,
        [pixels](std::size_t index) { return pixels[index]; });
    const auto output = impl_->yolo->InferSingleFloatTensor(input);
    if (output.shape.empty() || output.shape.back() < 6)
        throw std::runtime_error("YOLO output lacks box, objectness, or classes");

    infer::SDetectionRowSchema schema;
    schema.objectness_index = 4;
    schema.first_class_score_index = 5;
    schema.class_score_count = static_cast<std::size_t>(output.shape.back() - 5);
    const auto candidates = infer::DecodeDetectionRows(output, schema, 0.25f, 1000);
    std::vector<SDetectionBox> accepted;
    accepted.reserve(std::min<std::size_t>(candidates.size(), 50));
    const double scale_x = static_cast<double>(bgr.cols) / size_px.width;
    const double scale_y = static_cast<double>(bgr.rows) / size_px.height;
    for (const auto& candidate : candidates)
    {
        const auto& box = candidate.bounds;
        SDetectionBox mapped;
        mapped.bounds_px = {(box.center.x - box.size.width / 2) * scale_x,
                            (box.center.y - box.size.height / 2) * scale_y,
                            box.size.width * scale_x, box.size.height * scale_y};
        mapped.class_id = candidate.classification.class_id;
        mapped.score = candidate.classification.score;
        const bool duplicate = std::any_of(
            accepted.begin(), accepted.end(),
            [&mapped](const SDetectionBox& existing)
            {
                return mapped.class_id == existing.class_id &&
                       IntersectionOverUnion(mapped.bounds_px, existing.bounds_px) > 0.45f;
            });
        if (!duplicate)
            accepted.push_back(mapped);
        if (accepted.size() == 50)
            break;
    }
    return accepted;
}
} // namespace demo
