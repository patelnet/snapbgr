# Model Downloads — Background Removal ONNX Models

**No model binary is committed to this repository** — the checked-in
`modnet.onnx` is a zero-byte placeholder. Until you install a real model,
the app runs with a deterministic synthetic mask (a soft centered ellipse)
so the full pipeline stays testable.

## Installing a model — any filename works

You do **not** need to rename downloaded models. The app looks for models
in this order:

1. The model chosen via the tray menu **Select Model…** (persisted in
   settings).
2. Any `*.onnx` file in `%LOCALAPPDATA%\SnapBGR\models\`
   (create the folder and drop a model in — no rename needed).
3. Any `*.onnx` file next to `SnapBGR.exe`.

The preprocessing recipe (input resolution, normalization, sigmoid) is
detected automatically from the model's filename and its declared input
shape — so keep the original filename (e.g. `u2netp.onnx`,
`isnet-general-use.onnx`, `BiRefNet-portrait-epoch_150.onnx`).

## Recommended models

| Model | Download | Size | License | Best for |
|---|---|---|---|---|
| **U²-Netp** ⚡ | [u2netp.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx) | ~4.6 MB | Apache-2.0 ✅ | Fast general use; great first model |
| **Silueta** | [silueta.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/silueta.onnx) | ~43 MB | Apache-2.0 ✅ | Best size/quality trade-off |
| **MODNet** | [modnet.onnx (Hugging Face)](https://huggingface.co/gradio/Modnet/blob/main/modnet.onnx) | ~26 MB | Apache-2.0 ✅ | Portraits, hair detail |
| **IS-Net General** | [isnet-general-use.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/isnet-general-use.onnx) | ~176 MB | Apache-2.0 ✅ | Sharp, precise contours |
| **BiRefNet-General** | [BiRefNet-general-epoch_244.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-epoch_244.onnx) | large | MIT ✅ | Highest quality general use |

## Full catalog

### U²-Net family — Apache-2.0 (commercial OK)

Source: [xuebinqin/U-2-Net](https://github.com/xuebinqin/U-2-Net); ONNX
exports hosted by the [rembg model zoo](https://github.com/danielgatis/rembg)
([release v0.0.0](https://github.com/danielgatis/rembg/releases/tag/v0.0.0)).
Input 320×320, ImageNet normalization (auto-detected).

| Model | Download | Size | Best for |
|---|---|---|---|
| U²-Net | [u2net.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx) | ~176 MB | General objects, animals, people |
| U²-Netp (lite) | [u2netp.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx) | ~4.6 MB | Fast CPU inference |
| U²-Net Human Seg | [u2net_human_seg.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net_human_seg.onnx) | ~176 MB | People/body segmentation |
| Silueta | [silueta.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/silueta.onnx) | ~43 MB | Compressed U²-Net |

> Not supported: `u2net_cloth_seg.onnx` — it outputs clothing class labels,
> not an alpha matte.

### MODNet — Apache-2.0 (commercial OK)

Source: [ZHKKKe/MODNet](https://github.com/ZHKKKe/MODNet). No official
pre-built ONNX release; export from checkpoint with
[onnx/export_onnx.py](https://github.com/ZHKKKe/MODNet/tree/master/onnx),
or use the community export at
[huggingface.co/gradio/Modnet](https://huggingface.co/gradio/Modnet)
(~26 MB). Dynamic input size (the app uses 512×512), normalization to
[-1, 1]. Best-in-class for portrait matting and hair detail.

### IS-Net / DIS — Apache-2.0 (commercial OK)

Source: [xuebinqin/DIS](https://github.com/xuebinqin/DIS). Input
1024×1024.

| Model | Download | Size | Best for |
|---|---|---|---|
| IS-Net General | [isnet-general-use.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/isnet-general-use.onnx) | ~176 MB | Precise, sharp contours on any object |
| IS-Net Anime | [isnet-anime.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/isnet-anime.onnx) | ~176 MB | Anime / illustrated characters |

### BiRefNet family — MIT (commercial OK)

Source: [ZhengPeng7/BiRefNet](https://github.com/ZhengPeng7/BiRefNet).
Current state of the art. Input 1024×1024, ImageNet normalization, sigmoid
postprocess (all auto-detected). Large models — expect slower CPU
inference.

| Model | Download | Best for |
|---|---|---|
| General | [BiRefNet-general-epoch_244.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-epoch_244.onnx) | Best general quality |
| General Lite | [BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx) | Faster, smaller backbone |
| Portrait | [BiRefNet-portrait-epoch_150.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-portrait-epoch_150.onnx) | Portraits, fine hair |
| DIS | [BiRefNet-DIS-epoch_590.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-DIS-epoch_590.onnx) | Pixel-perfect product edges |
| HRSOD | [BiRefNet-HRSOD_DHU-epoch_115.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-HRSOD_DHU-epoch_115.onnx) | High-res complex scenes |
| COD | [BiRefNet-COD-epoch_125.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-COD-epoch_125.onnx) | Camouflaged objects |
| Massive | [BiRefNet-massive-TR_DIS5K_TR_TEs-epoch_420.onnx](https://github.com/danielgatis/rembg/releases/download/v0.0.0/BiRefNet-massive-TR_DIS5K_TR_TEs-epoch_420.onnx) | Best generalization |

### BRIA RMBG — ⚠️ NON-COMMERCIAL license

Commercial use requires a paid license from
[BRIA AI](https://bria.ai/bria-huggingface-model-license-agreement/).
Personal/research use only otherwise.

| Model | Download | Size | Notes |
|---|---|---|---|
| RMBG-1.4 | [model.onnx (Hugging Face)](https://huggingface.co/briaai/RMBG-1.4/resolve/main/onnx/model.onnx) — rename suggestion: `rmbg-1.4.onnx` | ~176 MB | Input 1024×1024 |
| RMBG-2.0 | [huggingface.co/briaai/RMBG-2.0](https://huggingface.co/briaai/RMBG-2.0) (gated — HF login + license required) | 234 MB–1 GB | BiRefNet-based, SOTA |

> RMBG downloads arrive named `model.onnx`; give them a name containing
> `rmbg` (e.g. `rmbg-1.4.onnx`) so the app picks the correct normalization.

## Auto-detected recipes (reference)

| Filename contains | Input | Normalization | Sigmoid |
|---|---|---|---|
| `birefnet`, `rmbg-2`, `rmbg2` | 1024×1024 | ImageNet mean/std | Yes |
| `isnet-anime` | 1024×1024 | ImageNet mean, std 1 | No |
| `isnet`, `dis`, `rmbg` | 1024×1024 | mean 0.5, std 1 | No |
| `u2net`, `u2netp`, `silueta` | 320×320 | ImageNet mean/std | No |
| `inspyrenet` | 1280×1024 | ImageNet mean/std | No |
| `modnet` or anything else | 512×512 | [-1, 1] (mean 0.5, std 0.5) | No |

When the ONNX graph declares static input dims, those override the
family's default resolution. Models must be single-input NCHW float32 RGB
with a `1×1×H×W` float matte output.

## Checksum verification (recommended)

```powershell
Get-FileHash .\<model>.onnx -Algorithm SHA256
```

rembg publishes MD5 checksums in its
[session sources](https://github.com/danielgatis/rembg/tree/main/rembg/sessions);
Hugging Face shows SHA256 on each file's blob page.

## License checklist — complete BEFORE redistributing

- [ ] Read the model's license (links above; verify for the exact file you
      downloaded).
- [ ] Confirm the license permits your use case — **BRIA RMBG models are
      non-commercial without a paid license.**
- [ ] Confirm redistribution terms if you plan to ship the model with an
      installer — many licenses require attribution files.
- [ ] Record the model version, source URL, and SHA256:
  - Version: `<fill in>`
  - Source: `<fill in>`
  - SHA256: `<fill in>`
- [ ] Check training-data/usage restrictions (e.g., face-data clauses).
- [ ] Add required attribution to your product's third-party notices.
