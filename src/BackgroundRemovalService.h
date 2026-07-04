// BackgroundRemovalService.h — image background removal via ONNX Runtime
// with a deterministic synthetic fallback.
//
// The service adapts to different model families automatically:
//   - The input resolution is read from the ONNX graph (static dims), or
//     falls back to 512x512 for dynamic-shape models such as MODNet.
//   - The normalization recipe and sigmoid postprocess are chosen from the
//     model filename (see DetectProfile), so u2net.onnx, isnet-general-use
//     .onnx, BiRefNet-*.onnx, silueta.onnx etc. work without renaming.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

// Forward-declare ORT types so headers stay light.
namespace Ort {
struct Env;
struct Session;
} // namespace Ort

namespace rmbg {

// Per-model preprocessing/postprocessing recipe. All known families can be
// expressed as: resize to WxH, x/255, then (x - mean[c]) / std[c], with an
// optional sigmoid on the raw output logits.
struct ModelProfile {
    int inputWidth = 512;
    int inputHeight = 512;
    float mean[3] = {0.5f, 0.5f, 0.5f}; // RGB order, on [0,1] scale
    float stdev[3] = {0.5f, 0.5f, 0.5f};
    bool applySigmoid = false; // BiRefNet / RMBG-2.0 emit raw logits
    std::wstring family = L"MODNet";
};

// Runs the background-removal pipeline:
//   preprocess (OpenCV) -> inference (ONNX Runtime) -> postprocess -> BGRA PNG.
//
// If no valid model is available the service produces a deterministic
// synthetic mask (centered soft ellipse) so the full pipeline can be
// exercised end-to-end without shipping model binaries.
class BackgroundRemovalService {
public:
    // Fallback geometry when the model has dynamic input dims (MODNet) or
    // no model is loaded (synthetic mask).
    static constexpr int kDefaultModelSize = 512;

    BackgroundRemovalService() = default;
    ~BackgroundRemovalService();

    BackgroundRemovalService(const BackgroundRemovalService&) = delete;
    BackgroundRemovalService& operator=(const BackgroundRemovalService&) = delete;

    // Attempts to load an ONNX model (any filename). Returns true on
    // success. On failure the service remains usable via the synthetic
    // fallback. The preprocessing profile is derived from the file name and
    // the model's declared input shape.
    bool LoadModel(const std::wstring& modelPath);

    bool IsModelLoaded() const noexcept { return m_modelLoaded; }

    // Active preprocessing recipe (family name, input size, norm).
    const ModelProfile& Profile() const noexcept { return m_profile; }

    // Short human-readable profile summary for status displays,
    // e.g. L"U2-Net norm, 320x320".
    std::wstring ProfileSummary() const;

    // Picks the normalization family from a model file name. Public and
    // static so it is unit-testable. `declaredWidth`/`declaredHeight` are
    // the static dims read from the ONNX graph (0 when dynamic) and are
    // used as a fallback signal when the name is not recognized.
    static ModelProfile DetectProfile(const std::wstring& modelPath,
                                      int declaredWidth, int declaredHeight);

    // Processes `inputPath` and writes a BGRA PNG to `outputDir`.
    // The output filename is "<stem>_nobg_<timestamp>.png" — timestamps
    // avoid ever overwriting existing user files.
    // Returns the full output path, or std::nullopt on failure.
    std::optional<std::wstring> ProcessImage(const std::wstring& inputPath,
                                             const std::wstring& outputDir);

    // --- Pipeline stages, public for testing --------------------------------

    // Resize to the profile's input size, BGR->RGB, float32, normalize with
    // the profile's mean/std, HWC->CHW. Returns a CHW float tensor packed
    // in a 1xN Mat (N = 3*W*H).
    cv::Mat Preprocess(const cv::Mat& bgr) const;

    // Runs inference; returns a cloned CV_32FC1 mask in [0,1] at the
    // model's output resolution. Falls back to GenerateSyntheticMask()
    // when no model is loaded or inference throws.
    cv::Mat RunInference(const cv::Mat& chwTensor);

    // Deterministic fallback: soft centered ellipse. Same input size ->
    // same output, which makes tests reproducible.
    static cv::Mat GenerateSyntheticMask();

    // Resize mask (any resolution) to `originalBgr.size()`, Gaussian-blur
    // the edges, convert to 8-bit, and merge with the original BGR image
    // into a BGRA result. Returns a cloned Mat.
    static cv::Mat Postprocess(const cv::Mat& originalBgr, const cv::Mat& mask);

private:
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    ModelProfile m_profile;
    bool m_modelLoaded = false;
};

} // namespace rmbg
