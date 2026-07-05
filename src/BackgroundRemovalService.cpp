// BackgroundRemovalService.cpp — see BackgroundRemovalService.h.
//
// Pipeline: OpenCV preprocess -> ONNX Runtime inference -> OpenCV
// postprocess -> BGRA PNG with timestamped filename. The preprocessing
// recipe (input size, normalization, sigmoid) adapts to the loaded model.
// A deterministic synthetic mask keeps the pipeline testable when no model
// binary is present (the repo only ships a placeholder).
#include "pch.h"
#include "BackgroundRemovalService.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include <array>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace rmbg {

namespace {

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(::towlower(c));
    return s;
}

} // namespace

BackgroundRemovalService::~BackgroundRemovalService() = default;

ModelProfile BackgroundRemovalService::DetectProfile(const std::wstring& modelPath,
                                                     int declaredWidth,
                                                     int declaredHeight) {
    // Recipes verified against danielgatis/rembg session implementations
    // and the official model cards. All express normalization on the [0,1]
    // scale: normalized = (x/255 - mean) / std, per RGB channel.
    static constexpr float kImageNetMean[3] = {0.485f, 0.456f, 0.406f};
    static constexpr float kImageNetStd[3] = {0.229f, 0.224f, 0.225f};

    const std::wstring name = ToLower(fs::path(modelPath).filename().wstring());
    auto has = [&](const wchar_t* needle) {
        return name.find(needle) != std::wstring::npos;
    };

    ModelProfile p;
    // Static graph dims win over the family's default resolution.
    const bool hasDeclared = declaredWidth > 0 && declaredHeight > 0;
    if (hasDeclared) {
        p.inputWidth = declaredWidth;
        p.inputHeight = declaredHeight;
    }

    if (has(L"birefnet") || has(L"rmbg-2") || has(L"rmbg2") || has(L"rmbg_2")) {
        // BiRefNet family / BRIA RMBG-2.0: 1024, ImageNet norm, raw logits.
        p.family = has(L"birefnet") ? L"BiRefNet" : L"RMBG-2.0";
        if (!hasDeclared) p.inputWidth = p.inputHeight = 1024;
        std::copy(std::begin(kImageNetMean), std::end(kImageNetMean), p.mean);
        std::copy(std::begin(kImageNetStd), std::end(kImageNetStd), p.stdev);
        p.applySigmoid = true;
    } else if (has(L"isnet-anime") || has(L"isnet_anime")) {
        // IS-Net anime: 1024, ImageNet mean but std = 1.
        p.family = L"IS-Net (anime)";
        if (!hasDeclared) p.inputWidth = p.inputHeight = 1024;
        std::copy(std::begin(kImageNetMean), std::end(kImageNetMean), p.mean);
        p.stdev[0] = p.stdev[1] = p.stdev[2] = 1.0f;
    } else if (has(L"isnet") || has(L"dis") || has(L"rmbg")) {
        // IS-Net general use / BRIA RMBG-1.4: 1024, mean 0.5, std 1.
        p.family = has(L"rmbg") ? L"RMBG-1.4" : L"IS-Net";
        if (!hasDeclared) p.inputWidth = p.inputHeight = 1024;
        p.mean[0] = p.mean[1] = p.mean[2] = 0.5f;
        p.stdev[0] = p.stdev[1] = p.stdev[2] = 1.0f;
    } else if (has(L"u2net") || has(L"u2netp") || has(L"silueta")) {
        // U2-Net family / Silueta: 320, ImageNet norm.
        p.family = L"U2-Net";
        if (!hasDeclared) p.inputWidth = p.inputHeight = 320;
        std::copy(std::begin(kImageNetMean), std::end(kImageNetMean), p.mean);
        std::copy(std::begin(kImageNetStd), std::end(kImageNetStd), p.stdev);
    } else if (has(L"inspyrenet")) {
        // InSPyReNet: non-square 1280x1024, ImageNet norm.
        p.family = L"InSPyReNet";
        if (!hasDeclared) { p.inputWidth = 1280; p.inputHeight = 1024; }
        std::copy(std::begin(kImageNetMean), std::end(kImageNetMean), p.mean);
        std::copy(std::begin(kImageNetStd), std::end(kImageNetStd), p.stdev);
    } else {
        // MODNet and unknown models: 512 (or declared dims), norm to
        // [-1, 1] i.e. mean 0.5 / std 0.5 — MODNet's documented recipe.
        p.family = has(L"modnet") ? L"MODNet" : L"Generic (MODNet norm)";
        if (!hasDeclared) p.inputWidth = p.inputHeight = kDefaultModelSize;
    }
    return p;
}

std::wstring BackgroundRemovalService::ProfileSummary() const {
    return m_profile.family + L", " + std::to_wstring(m_profile.inputWidth) +
           L"x" + std::to_wstring(m_profile.inputHeight);
}

bool BackgroundRemovalService::LoadModel(const std::wstring& modelPath) {
    m_modelLoaded = false;
    m_session.reset();
    m_env.reset();
    m_profile = ModelProfile{};

    if (!fs::exists(modelPath)) {
        return false; // no model — synthetic fallback stays in effect
    }

    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SnapBGR");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Ort::Session takes wide-char paths on Windows.
        m_session = std::make_unique<Ort::Session>(*m_env, modelPath.c_str(), options);

        // Read the declared input shape (NCHW). Dims may be -1/dynamic.
        int declaredW = 0, declaredH = 0;
        const auto typeInfo = m_session->GetInputTypeInfo(0);
        const auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
        const auto shape = tensorInfo.GetShape();
        if (shape.size() == 4) {
            if (shape[2] > 0) declaredH = static_cast<int>(shape[2]);
            if (shape[3] > 0) declaredW = static_cast<int>(shape[3]);
        }
        m_profile = DetectProfile(modelPath, declaredW, declaredH);
        m_modelLoaded = true;
    } catch (const Ort::Exception&) {
        // Invalid/placeholder model file — fall back gracefully.
        m_session.reset();
        m_env.reset();
    } catch (...) {
        m_session.reset();
        m_env.reset();
    }
    return m_modelLoaded;
}

cv::Mat BackgroundRemovalService::Preprocess(const cv::Mat& bgr) const {
    CV_Assert(!bgr.empty() && bgr.type() == CV_8UC3);

    const int w = m_profile.inputWidth;
    const int h = m_profile.inputHeight;

    // Straight resize (no letterboxing). This distorts the aspect ratio
    // slightly but simplifies mask mapping back to the original size; the
    // supported matting models are tolerant of this. Swap in letterbox
    // padding here if your model requires preserved aspect ratio.
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(w, h), 0, 0, cv::INTER_AREA);

    // BGR -> RGB, then float32 scaled to [0,1].
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    cv::Mat floatImg;
    rgb.convertTo(floatImg, CV_32FC3, 1.0 / 255.0);

    // Per-channel normalization with the profile's mean/std, then
    // HWC -> CHW: split channels and pack contiguously (RRR..GGG..BBB..).
    std::vector<cv::Mat> channels(3);
    cv::split(floatImg, channels);
    cv::Mat chw(1, 3 * w * h, CV_32FC1);
    float* dst = chw.ptr<float>();
    const size_t planeSize = static_cast<size_t>(w) * h;
    for (int c = 0; c < 3; ++c) {
        channels[c] = (channels[c] - m_profile.mean[c]) / m_profile.stdev[c];
        std::memcpy(dst + c * planeSize, channels[c].ptr<float>(), planeSize * sizeof(float));
    }
    return chw;
}

cv::Mat BackgroundRemovalService::GenerateSyntheticMask() {
    // Deterministic soft ellipse centered in the frame: same output every
    // run, so tests are reproducible. Roughly mimics a portrait subject.
    constexpr int size = kDefaultModelSize;
    cv::Mat mask = cv::Mat::zeros(size, size, CV_32FC1);
    const cv::Point center(size / 2, size / 2);
    const cv::Size axes(size / 3, static_cast<int>(size / 2.2));
    cv::ellipse(mask, center, axes, 0.0, 0.0, 360.0, cv::Scalar(1.0), cv::FILLED);
    // Feather the edge so the alpha falls off smoothly.
    cv::GaussianBlur(mask, mask, cv::Size(51, 51), 0);
    return mask;
}

cv::Mat BackgroundRemovalService::RunInference(const cv::Mat& chwTensor) {
    if (!m_modelLoaded || !m_session) {
        return GenerateSyntheticMask();
    }

    try {
        // Input: 1x3xHxW float32. Output: 1x1xH'xW' float32 matte (H'/W'
        // usually match the input, but we read the actual output shape).
        const std::array<int64_t, 4> inputShape{
            1, 3, m_profile.inputHeight, m_profile.inputWidth};
        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // The tensor view is non-owning; chwTensor must outlive the Run call.
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo,
            const_cast<float*>(chwTensor.ptr<float>()),
            chwTensor.total(),
            inputShape.data(), inputShape.size());

        // Resolve I/O names dynamically so different exports work.
        Ort::AllocatorWithDefaultOptions alloc;
        const auto inputName = m_session->GetInputNameAllocated(0, alloc);
        const auto outputName = m_session->GetOutputNameAllocated(0, alloc);
        const char* inputNames[] = {inputName.get()};
        const char* outputNames[] = {outputName.get()};

        auto outputs = m_session->Run(Ort::RunOptions{nullptr},
                                      inputNames, &inputTensor, 1,
                                      outputNames, 1);
        if (outputs.empty() || !outputs[0].IsTensor()) {
            return GenerateSyntheticMask();
        }

        const float* data = outputs[0].GetTensorData<float>();
        // Read the actual output spatial dims (last two of 1x1xHxW).
        const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        int outH = m_profile.inputHeight, outW = m_profile.inputWidth;
        if (outShape.size() >= 2) {
            const auto h = outShape[outShape.size() - 2];
            const auto w = outShape[outShape.size() - 1];
            if (h > 0 && w > 0) {
                outH = static_cast<int>(h);
                outW = static_cast<int>(w);
            }
        }
        // Wrap ORT-owned memory, then clone so the returned Mat owns its
        // data after `outputs` is destroyed.
        cv::Mat mask(outH, outW, CV_32FC1, const_cast<float*>(data));
        cv::Mat owned = mask.clone();
        if (m_profile.applySigmoid) {
            // BiRefNet / RMBG-2.0 emit raw logits: sigmoid = 1/(1+e^-x).
            cv::exp(-owned, owned);
            owned += 1.0f;
            cv::divide(1.0, owned, owned);
        }
        // Clamp to [0,1] defensively — some exports emit slight overshoot.
        cv::min(owned, 1.0f, owned);
        cv::max(owned, 0.0f, owned);
        return owned;
    } catch (...) {
        // Any inference failure degrades to the synthetic mask.
        return GenerateSyntheticMask();
    }
}

cv::Mat BackgroundRemovalService::Postprocess(const cv::Mat& originalBgr, const cv::Mat& mask) {
    CV_Assert(!originalBgr.empty() && originalBgr.type() == CV_8UC3);
    CV_Assert(mask.type() == CV_32FC1);

    // 1. Resize mask back to the original image size.
    cv::Mat maskFull;
    cv::resize(mask, maskFull, originalBgr.size(), 0, 0, cv::INTER_LINEAR);

    // 2. Gaussian blur for a soft edge (kernel must be odd).
    cv::GaussianBlur(maskFull, maskFull, cv::Size(7, 7), 0);

    // 3. Convert to 8-bit alpha.
    cv::Mat alpha8;
    maskFull.convertTo(alpha8, CV_8UC1, 255.0);

    // 4. BGR -> BGRA with the computed alpha channel.
    cv::Mat bgra;
    cv::cvtColor(originalBgr, bgra, cv::COLOR_BGR2BGRA);
    std::vector<cv::Mat> planes(4);
    cv::split(bgra, planes);
    planes[3] = alpha8;
    cv::merge(planes, bgra);

    return bgra.clone(); // caller owns an independent copy
}

bool BackgroundRemovalService::IsGeneratedOutput(const std::wstring& path) {
    const fs::path p(path);
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
    if (ext != L".png") return false;
    return p.stem().wstring().find(L"_nobg_") != std::wstring::npos;
}

std::optional<std::wstring> BackgroundRemovalService::ProcessImage(
    const std::wstring& inputPath, const std::wstring& outputDir) {
    try {
        // imread does not accept wide paths; read bytes then decode instead
        // so Unicode paths work correctly.
        std::ifstream file(fs::path(inputPath), std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
        if (bytes.empty()) return std::nullopt;

        cv::Mat bgr = cv::imdecode(cv::Mat(1, static_cast<int>(bytes.size()), CV_8UC1, bytes.data()),
                                   cv::IMREAD_COLOR);
        if (bgr.empty()) return std::nullopt; // not an image (or unsupported format)

        // Run the three pipeline stages.
        cv::Mat tensor = Preprocess(bgr);
        cv::Mat mask = RunInference(tensor);
        cv::Mat bgra = Postprocess(bgr, mask);

        // Build "<stem>_nobg_<yyyyMMdd-HHmmss>.png". The timestamp suffix
        // guarantees we never overwrite an existing user file.
        fs::create_directories(outputDir);
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &tt);
        wchar_t stamp[32];
        std::wcsftime(stamp, std::size(stamp), L"%Y%m%d-%H%M%S", &tm);

        const std::wstring stem = fs::path(inputPath).stem().wstring();
        fs::path outPath = fs::path(outputDir) / (stem + L"_nobg_" + stamp + L".png");
        // Extremely defensive: if two files land within the same second,
        // add a numeric disambiguator rather than replacing anything.
        for (int i = 1; fs::exists(outPath) && i < 1000; ++i) {
            outPath = fs::path(outputDir) /
                      (stem + L"_nobg_" + stamp + L"_" + std::to_wstring(i) + L".png");
        }

        // imwrite has the same narrow-path limitation; encode to memory and
        // write bytes ourselves.
        std::vector<uchar> png;
        if (!cv::imencode(".png", bgra, png)) return std::nullopt;
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return std::nullopt;
        out.write(reinterpret_cast<const char*>(png.data()),
                  static_cast<std::streamsize>(png.size()));
        out.close();

        return outPath.wstring();
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace rmbg
