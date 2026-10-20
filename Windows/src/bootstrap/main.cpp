#include <cstdlib>
#include <iostream>

extern "C" {
#include "kiss_fftr.h"
#include "tensorflow/lite/c/c_api.h"
}

namespace {
constexpr int kRequiredFftSize = 1536;
}

int main() {
    std::cout << "CleanWave Windows bootstrap\n";
    std::cout << "TensorFlow Lite runtime: " << TfLiteVersion() << '\n';

    kiss_fftr_cfg forward = kiss_fftr_alloc(kRequiredFftSize, 0, nullptr, nullptr);
    kiss_fftr_cfg inverse = kiss_fftr_alloc(kRequiredFftSize, 1, nullptr, nullptr);
    if (forward == nullptr || inverse == nullptr) {
        if (forward != nullptr) {
            kiss_fftr_free(forward);
        }
        if (inverse != nullptr) {
            kiss_fftr_free(inverse);
        }
        std::cerr << "KissFFT could not create the required 1536-point real FFT plans.\n";
        return EXIT_FAILURE;
    }

    kiss_fftr_free(forward);
    kiss_fftr_free(inverse);

    std::cout << "KissFFT real FFT size 1536: plan creation OK\n";
    std::cout << "Audio model loading: intentionally not implemented in Step 2\n";
    std::cout << "Streaming DSP/live audio: intentionally not implemented in Step 2\n";
    return EXIT_SUCCESS;
}

