/**
 * @file camera_stream_demo.cpp
 * @brief Run space-aware KLT on webcam, video, or naturally ordered image streams.
 */
#include "demo_core.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

struct SOptions
{
    std::optional<int> camera_index;
    std::filesystem::path video;
    std::filesystem::path frames_dir;
    std::filesystem::path centroid_model;
    std::filesystem::path yolo_model;
    std::filesystem::path output_dir;
    demo::EMode mode{demo::EMode::Klt};
    demo::EKltExtraction extraction{demo::EKltExtraction::Space};
    std::uint32_t max_features{demo::default_max_features};
    std::uint32_t max_new_features{demo::default_max_new_features};
    std::uint32_t max_frames{0};
    double fps{15.0};
    bool headless{false};
};

SOptions ParseOptions(int argc, char** argv)
{
    SOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string key(argv[index]);
        if (key == "--help")
        {
            std::cout << "camera_stream_demo --camera-index N | --video FILE | --frames-dir DIR\n"
                         "  [--mode klt|centroid|both] [--klt-extraction space|generic]\n"
                         "  [--max-features 1..100] [--max-new-features 1..25]\n"
                         "  [--centroid-model ONNX] [--yolo-model PTAFMODEL]\n"
                         "  [--fps N] [--max-frames N] [--headless] [--output-dir DIR]\n";
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
        if (key == "--camera-index")
            options.camera_index = std::stoi(value);
        else if (key == "--video")
            options.video = value;
        else if (key == "--frames-dir")
            options.frames_dir = value;
        else if (key == "--centroid-model")
            options.centroid_model = value;
        else if (key == "--yolo-model")
            options.yolo_model = value;
        else if (key == "--output-dir")
            options.output_dir = value;
        else if (key == "--max-features")
            options.max_features = demo::ParsePositiveCount(value, key.c_str());
        else if (key == "--max-new-features")
            options.max_new_features = demo::ParsePositiveCount(value, key.c_str());
        else if (key == "--max-frames")
            options.max_frames = demo::ParsePositiveCount(value, key.c_str());
        else if (key == "--fps")
            options.fps = std::stod(value);
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
        else if (key == "--klt-extraction")
        {
            if (value == "space")
                options.extraction = demo::EKltExtraction::Space;
            else if (value == "generic")
                options.extraction = demo::EKltExtraction::Generic;
            else
                throw std::invalid_argument("Unknown KLT extraction policy: " + value);
        }
        else
            throw std::invalid_argument("Unknown option: " + key);
    }
    const int source_count = static_cast<int>(options.camera_index.has_value()) +
                             static_cast<int>(!options.video.empty()) +
                             static_cast<int>(!options.frames_dir.empty());
    if (source_count != 1)
        throw std::invalid_argument("Select exactly one camera source");
    demo::ValidateKltFeatureLimits(options.max_features, options.max_new_features);
    if (!(options.fps > 0.0) || !std::isfinite(options.fps))
        throw std::invalid_argument("--fps must be positive and finite");
    if (options.mode != demo::EMode::Klt && options.centroid_model.empty())
        throw std::invalid_argument("Centroid and both modes require --centroid-model");
    if (options.mode == demo::EMode::Klt && !options.centroid_model.empty())
        throw std::invalid_argument("Use --mode both to run KLT and centroiding together");
#ifndef DEMO_ENABLE_ML
    if (options.mode != demo::EMode::Klt || !options.yolo_model.empty())
        throw std::invalid_argument("Models require DEMO_ENABLE_ML=ON");
#endif
    return options;
}

bool IsImageFile(const std::filesystem::path& path)
{
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char character) { return std::tolower(character); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" ||
           ext == ".tiff";
}

bool NaturalLess(const std::filesystem::path& left, const std::filesystem::path& right)
{
    const auto a = left.filename().string();
    const auto b = right.filename().string();
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (std::isdigit(static_cast<unsigned char>(a[i])) &&
            std::isdigit(static_cast<unsigned char>(b[j])))
        {
            const auto start_i = i, start_j = j;
            while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i])))
                ++i;
            while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j])))
                ++j;
            const auto run_a = a.substr(start_i, i - start_i);
            const auto run_b = b.substr(start_j, j - start_j);
            const auto significant_a = run_a.find_first_not_of('0');
            const auto significant_b = run_b.find_first_not_of('0');
            const auto value_a =
                significant_a == std::string::npos ? "0" : run_a.substr(significant_a);
            const auto value_b =
                significant_b == std::string::npos ? "0" : run_b.substr(significant_b);
            if (value_a.size() != value_b.size())
                return value_a.size() < value_b.size();
            if (value_a != value_b)
                return value_a < value_b;
            if (run_a.size() != run_b.size())
                return run_a.size() < run_b.size();
        }
        else
        {
            const auto ca = std::tolower(static_cast<unsigned char>(a[i++]));
            const auto cb = std::tolower(static_cast<unsigned char>(b[j++]));
            if (ca != cb)
                return ca < cb;
        }
    }
    return a.size() < b.size();
}

std::vector<std::filesystem::path> ListFrames(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory))
        throw std::invalid_argument("Frames directory does not exist: " + directory.string());
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && IsImageFile(entry.path()))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end(), NaturalLess);
    if (files.empty())
        throw std::invalid_argument("Frames directory has no supported images");
    return files;
}

void Capture(const SOptions& options, demo::CLatestMailbox<demo::SSourceFrame>& mailbox,
             const std::atomic<bool>& stop)
{
    const auto start = Clock::now();
    cv::VideoCapture capture;
    std::vector<std::filesystem::path> files;
    if (options.camera_index)
        capture.open(*options.camera_index);
    else if (!options.video.empty())
        capture.open(options.video.string());
    else
        files = ListFrames(options.frames_dir);
    if (files.empty() && !capture.isOpened())
        throw std::runtime_error("Cannot open camera or video");

    cv::Size source_size_px;
    for (std::uint64_t source_index = 0; !stop; ++source_index)
    {
        if (options.max_frames && source_index >= options.max_frames)
            break;
        if (!files.empty() && source_index >= files.size())
            break;
        const auto deadline =
            start + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(source_index / options.fps));
        if (!options.camera_index)
            std::this_thread::sleep_until(deadline);

        const auto acquisition_start = Clock::now();
        cv::Mat image;
        if (!files.empty())
            image = cv::imread(files[source_index].string(), cv::IMREAD_COLOR);
        else if (!capture.read(image))
            break;
        if (image.empty())
            throw std::runtime_error("Source yielded an empty image");
        if (source_size_px.empty())
            source_size_px = image.size();
        if (image.size() != source_size_px)
            throw std::runtime_error("Source image dimensions changed within the stream");
        const double timestamp_s = std::chrono::duration<double>(Clock::now() - start).count();
        const double acquisition_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - acquisition_start).count();
        demo::SSourceFrame frame;
        frame.image = std::move(image);
        frame.source_index = source_index;
        frame.timestamp_s = timestamp_s;
        frame.acquisition_ms = acquisition_ms;
        mailbox.publish(std::move(frame));
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
            window = std::make_unique<demo::CPreviewWindow>("Camera perception stream",
                                                            cv::Size{1200, 900});
        demo::CLatestMailbox<demo::SSourceFrame> incoming;
        demo::CLatestMailbox<demo::SPreviewFrame> outgoing;
        std::atomic<bool> stop{false};
        std::exception_ptr failure;
        double preview_total_ms = 0.0;
        std::uint64_t presented_frames = 0;
        std::mutex failure_mutex;
        const auto record_failure = [&]
        {
            std::lock_guard lock(failure_mutex);
            if (!failure)
                failure = std::current_exception();
            stop = true;
        };

        std::thread capture(
            [&]
            {
                try
                {
                    Capture(options, incoming, stop);
                }
                catch (...)
                {
                    record_failure();
                }
                incoming.close();
            });
        std::thread processing(
            [&]
            {
                try
                {
                    demo::CFrameWriter writer(options.output_dir);
                    std::optional<demo::CFrameProcessor> processor;
                    std::uint64_t processed_index = 0;
                    while (auto frame = incoming.take())
                    {
                        if (!processor)
                        {
                            processor.emplace(frame->image.size(), options.mode, options.extraction,
                                              options.max_features, options.max_new_features,
                                              std::nullopt, options.centroid_model,
                                              options.yolo_model);
                            const std::string source_kind = options.camera_index    ? "webcam"
                                                            : options.video.empty() ? "frames_dir"
                                                                                    : "video";
                            const std::string source_path =
                                options.camera_index    ? std::to_string(*options.camera_index)
                                : options.video.empty() ? options.frames_dir.string()
                                                        : options.video.string();
                            std::ostringstream metadata;
                            metadata
                                << "{\"schema\":1,\"program\":\"camera_stream_demo\""
                                << ",\"mode\":" << demo::JsonQuote(demo::ModeName(options.mode))
                                << ",\"klt_max_features\":" << options.max_features
                                << ",\"klt_max_new_features\":" << options.max_new_features
                                << ",\"klt_extraction\":"
                                << demo::JsonQuote(demo::ExtractionName(options.extraction))
                                << ",\"source_kind\":" << demo::JsonQuote(source_kind)
                                << ",\"source\":" << demo::JsonQuote(source_path)
                                << ",\"centroid_model\":"
                                << demo::JsonQuote(options.centroid_model.string())
                                << ",\"yolo_model\":"
                                << demo::JsonQuote(options.yolo_model.string())
                                << ",\"width_px\":" << frame->image.cols
                                << ",\"height_px\":" << frame->image.rows << '}';
                            writer.writeRunMetadata(metadata.str());
                        }
                        auto preview = processor->process(*frame, processed_index++,
                                                          incoming.dropped() + outgoing.dropped());
                        demo::LogFrameSummary(preview.summary);
                        writer.write(preview);
                        outgoing.publish(std::move(preview));
                    }
                }
                catch (...)
                {
                    record_failure();
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
            while (!window->shouldClose() && !outgoing.closedAndEmpty())
            {
                window->pollEvents();
                if (auto preview = outgoing.tryTake())
                {
                    preview_total_ms += window->show(preview->bgr);
                    ++presented_frames;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            stop = true;
        }

        capture.join();
        processing.join();
        if (presented_frames)
            std::cout << "Preview: " << presented_frames << " frames, mean "
                      << preview_total_ms / presented_frames << " ms per upload/draw/swap\n";
        if (failure)
            std::rethrow_exception(failure);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "camera_stream_demo: " << error.what() << '\n';
        return 1;
    }
}
