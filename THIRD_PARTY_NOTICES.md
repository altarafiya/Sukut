# Third-party notices

Sukut is developed by الطرفية. The following components retain their original
copyrights and license terms. See the original texts in `licenses/`, `app/vendor/`
and the font assets; release packages also carry component notices.

| Component | License / terms |
| --- | --- |
| TensorFlow Lite 2.16.1 | Apache 2.0; original license retained |
| KissFFT | BSD 3-Clause |
| FXdiv, pthreadpool | Original MIT/BSD notices retained |
| Abseil, RUY, gemmlowp | Original Apache 2.0 notices retained |
| Eigen | Original COPYING files retained |
| Windows App SDK, .NET and Microsoft runtime components | Original Microsoft licenses retained |
| CommunityToolkit WinUI | MIT |
| IBM Plex Sans Arabic, Readex Pro, Handjet | SIL Open Font License 1.1, alongside the fonts |
| Shizuku API/provider | MIT; upstream [Shizuku API](https://github.com/RikkaApps/Shizuku-API) |
| HiddenApiBypass | Apache 2.0; upstream [AndroidHiddenApiBypass](https://github.com/LSPosed/AndroidHiddenApiBypass) |
| JTransforms / JLargeArrays | Original BSD component terms |
| ONNX Runtime (legacy implementation) | MIT and original third-party notices |

The bundled Android TensorFlow Lite runtime has a read-only CPU display-label
adjustment, recorded in `app/vendor/RUNTIME_NOTES.json`. Executable sections and
numerical operations are unchanged. The original Apache 2.0 license is retained.

VB-CABLE is a separate VB-Audio product. The Windows installer includes its
unchanged original driver package and installs it only when absent. Its original
signature and terms are retained; removing Sukut leaves the shared driver installed.
See [VB-CABLE](https://vb-audio.com/Cable/) and its vendor licensing terms.

Shizuku and Android Platform Tools used by phone setup are obtained from their
official sources with pinned checksums. Their upstream terms continue to apply.
Inference assets in distributed application packages are third-party material
and remain subject to the applicable authorization and rights-holder terms.
