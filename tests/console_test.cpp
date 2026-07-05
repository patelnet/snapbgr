// console_test.cpp — smoke test for the background-removal pipeline.
//
// Runs BackgroundRemovalService::ProcessImage against a sample image using
// the synthetic-mask fallback (no model binary required) and verifies:
//   1. an output PNG is produced,
//   2. the PNG has an alpha channel,
//   3. the alpha channel is non-empty (contains both opaque-ish and
//      transparent-ish pixels).
//
// Usage: rmbg_console_test [path\to\sample.jpg]
// Exit code 0 = pass, nonzero = fail. Used by CI and `ctest`.
#include "pch.h"
#include "BackgroundRemovalService.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
// If no sample image is supplied (or it doesn't exist), synthesize one so
// the test is fully self-contained.
std::wstring EnsureSampleImage(const std::wstring& candidate, const fs::path& tempDir) {
    if (!candidate.empty() && fs::exists(candidate)) {
        return candidate;
    }
    std::wprintf(L"[test] Sample not found, generating a synthetic test image.\n");
    cv::Mat img(480, 640, CV_8UC3, cv::Scalar(40, 90, 160));
    cv::circle(img, {320, 240}, 120, cv::Scalar(220, 220, 240), cv::FILLED);
    cv::rectangle(img, {40, 40}, {600, 440}, cv::Scalar(20, 200, 90), 4);

    const fs::path path = tempDir / L"generated_sample.png";
    std::vector<uchar> buf;
    cv::imencode(".png", img, buf);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    return path.wstring();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const fs::path outDir = fs::temp_directory_path() / L"rmbg_console_test";
    fs::create_directories(outDir);

    const std::wstring input =
        EnsureSampleImage(argc > 1 ? argv[1] : L"", outDir);

    rmbg::BackgroundRemovalService service;
    // Deliberately attempt to load the placeholder; it is not a valid model,
    // so this must return false and enable the synthetic fallback.
    const bool modelLoaded = service.LoadModel(L"models\\modnet.onnx");
    std::wprintf(L"[test] Model loaded: %s (synthetic fallback %s)\n",
                 modelLoaded ? L"yes" : L"no",
                 modelLoaded ? L"inactive" : L"active");

    const auto result = service.ProcessImage(input, outDir.wstring());
    if (!result) {
        std::wprintf(L"[FAIL] ProcessImage returned no output path.\n");
        return 1;
    }
    std::wprintf(L"[test] Output: %s\n", result->c_str());

    // Re-read the PNG preserving alpha and validate.
    std::ifstream in(fs::path(*result), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    cv::Mat png = cv::imdecode(
        cv::Mat(1, static_cast<int>(bytes.size()), CV_8UC1, bytes.data()),
        cv::IMREAD_UNCHANGED);

    if (png.empty() || png.channels() != 4) {
        std::wprintf(L"[FAIL] Output is not a 4-channel BGRA PNG (channels=%d).\n",
                     png.empty() ? 0 : png.channels());
        return 2;
    }

    std::vector<cv::Mat> planes;
    cv::split(png, planes);
    const cv::Mat& alpha = planes[3];
    double minA = 0.0, maxA = 0.0;
    cv::minMaxLoc(alpha, &minA, &maxA);
    const double meanA = cv::mean(alpha)[0];
    std::wprintf(L"[test] Alpha stats: min=%.0f max=%.0f mean=%.1f\n", minA, maxA, meanA);

    // "Non-empty alpha": must contain real variation — the synthetic
    // ellipse yields opaque center + transparent corners.
    if (maxA < 200.0 || minA > 50.0) {
        std::wprintf(L"[FAIL] Alpha channel lacks expected variation.\n");
        return 3;
    }

    std::wprintf(L"[PASS] Pipeline produced a valid transparent PNG.\n");

    // --- JPG output mode: no alpha; subject composited onto white. ----------
    const auto jpgResult = service.ProcessImage(input, outDir.wstring(), L"jpg");
    if (!jpgResult) {
        std::wprintf(L"[FAIL] ProcessImage (jpg) returned no output path.\n");
        return 4;
    }
    std::wprintf(L"[test] JPG output: %s\n", jpgResult->c_str());
    if (fs::path(*jpgResult).extension() != L".jpg") {
        std::wprintf(L"[FAIL] JPG output does not have a .jpg extension.\n");
        return 5;
    }
    std::ifstream inJpg(fs::path(*jpgResult), std::ios::binary);
    std::vector<char> jpgBytes((std::istreambuf_iterator<char>(inJpg)),
                               std::istreambuf_iterator<char>());
    cv::Mat jpg = cv::imdecode(
        cv::Mat(1, static_cast<int>(jpgBytes.size()), CV_8UC1, jpgBytes.data()),
        cv::IMREAD_UNCHANGED);
    if (jpg.empty() || jpg.channels() != 3) {
        std::wprintf(L"[FAIL] JPG output is not a 3-channel image (channels=%d).\n",
                     jpg.empty() ? 0 : jpg.channels());
        return 6;
    }
    // The masked-out corners must have been filled with (near-)white.
    const cv::Vec3b corner = jpg.at<cv::Vec3b>(2, 2);
    if (corner[0] < 200 || corner[1] < 200 || corner[2] < 200) {
        std::wprintf(L"[FAIL] JPG background is not white (corner=%d,%d,%d).\n",
                     corner[0], corner[1], corner[2]);
        return 7;
    }

    std::wprintf(L"[PASS] JPG output mode produced a white-background image.\n");
    return 0;
}
