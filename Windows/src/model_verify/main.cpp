#include "model/SeparationModel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/register_ref.h"
#include "tensorflow/lite/model_builder.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/schema/schema_utils.h"

namespace {

using Model = cleanwave::SeparationModel;
using Spectral = Model::SpectralBuffer;
using State = Model::StateBuffer;

struct ReferenceOutputs {
  Spectral output0{};
  Spectral output1{};
  State output2{};
  Spectral output3{};
  Spectral output4{};
  Spectral output5{};
  Spectral output6{};
};

struct Metrics {
  double max_abs = 0.0;
  double rms = 0.0;
  double reference_max_abs = 0.0;
  double reference_rms = 0.0;
  double max_abs_ratio = 0.0;
  double rms_ratio = 0.0;
  double max_element_bound_ratio = 0.0;
  std::size_t worst_index = 0;
};

constexpr double kElementAbsTolerance = 5.0e-4;
constexpr double kElementRelTolerance = 1.0e-2;

template <typename Container>
Metrics Compare(const Container& lhs, const Container& rhs) {
  if (lhs.size() != rhs.size()) throw std::runtime_error("comparison size mismatch");
  long double squared = 0.0L;
  long double reference_squared = 0.0L;
  double max_abs = 0.0;
  double reference_max_abs = 0.0;
  double max_element_bound_ratio = 0.0;
  std::size_t worst_index = 0;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!std::isfinite(lhs[i]) || !std::isfinite(rhs[i])) {
      throw std::runtime_error("non-finite value encountered during numerical comparison");
    }
    const double difference =
        std::abs(static_cast<double>(lhs[i]) - static_cast<double>(rhs[i]));
    if (difference > max_abs) {
      max_abs = difference;
      worst_index = i;
    }
    reference_max_abs =
        std::max(reference_max_abs, std::abs(static_cast<double>(rhs[i])));
    const double element_bound =
        kElementAbsTolerance +
        kElementRelTolerance * std::abs(static_cast<double>(rhs[i]));
    max_element_bound_ratio =
        std::max(max_element_bound_ratio, difference / element_bound);
    squared += static_cast<long double>(difference) * difference;
    reference_squared += static_cast<long double>(rhs[i]) * rhs[i];
  }
  const double rms = std::sqrt(static_cast<double>(squared / lhs.size()));
  const double reference_rms =
      std::sqrt(static_cast<double>(reference_squared / lhs.size()));
  const double max_denominator = std::max(reference_max_abs, 1.0e-12);
  const double rms_denominator = std::max(reference_rms, 1.0e-12);
  return {max_abs,
          rms,
          reference_max_abs,
          reference_rms,
          max_abs / max_denominator,
          rms / rms_denominator,
          max_element_bound_ratio,
          worst_index};
}

template <typename Container>
double MaxAbs(const Container& values) {
  double result = 0.0;
  for (const float value : values) {
    result = std::max(result, std::abs(static_cast<double>(value)));
  }
  return result;
}

Spectral MakeInput(std::uint32_t seed) {
  Spectral result{};
  std::uint32_t state = seed;
  for (float& value : result) {
    state = state * 1664525U + 1013904223U;
    const int centered = static_cast<int>((state >> 8U) & 0xffffU) - 32768;
    value = static_cast<float>(centered) / 131072.0F;
  }
  return result;
}

std::string ShapeString(const TfLiteTensor* tensor) {
  std::ostringstream stream;
  stream << '[';
  for (int i = 0; i < tensor->dims->size; ++i) {
    if (i != 0) stream << ',';
    stream << tensor->dims->data[i];
  }
  stream << ']';
  return stream.str();
}

void RequireFloatTensor(const TfLiteTensor* tensor, const std::vector<int>& shape,
                        const std::string& label) {
  if (tensor == nullptr || tensor->type != kTfLiteFloat32) {
    throw std::runtime_error(label + " is not FLOAT32");
  }
  if (tensor->dims == nullptr || tensor->dims->size != static_cast<int>(shape.size())) {
    throw std::runtime_error(label + " rank mismatch");
  }
  for (std::size_t i = 0; i < shape.size(); ++i) {
    if (tensor->dims->data[i] != shape[i]) {
      throw std::runtime_error(label + " shape mismatch: " + ShapeString(tensor));
    }
  }
}

class ReferenceInterpreter {
 public:
  explicit ReferenceInterpreter(const std::filesystem::path& model_path) {
    std::ifstream stream(model_path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("reference model open failed");
    const std::streamoff size = stream.tellg();
    if (size <= 0) throw std::runtime_error("reference model is empty");
    model_bytes_.resize(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(model_bytes_.data()), size);
    if (!stream) throw std::runtime_error("reference model read failed");
    model_ = tflite::FlatBufferModel::VerifyAndBuildFromBuffer(
        reinterpret_cast<const char*>(model_bytes_.data()), model_bytes_.size());
    if (!model_) throw std::runtime_error("reference VerifyAndBuildFromBuffer failed");

    tflite::InterpreterBuilder builder(*model_, resolver_);
    if (builder.SetNumThreads(1) != kTfLiteOk) {
      throw std::runtime_error("reference SetNumThreads(1) failed");
    }
    if (builder(&interpreter_) != kTfLiteOk || !interpreter_) {
      throw std::runtime_error("reference InterpreterBuilder failed");
    }
    if (interpreter_->AllocateTensors() != kTfLiteOk) {
      throw std::runtime_error("reference AllocateTensors failed");
    }
    ValidateContract();
  }

  ReferenceOutputs Run(const Spectral& left, const Spectral& right,
                       const State& state) {
    std::memcpy(interpreter_->typed_input_tensor<float>(0), left.data(), sizeof(left));
    std::memcpy(interpreter_->typed_input_tensor<float>(1), right.data(), sizeof(right));
    std::memcpy(interpreter_->typed_input_tensor<float>(2), state.data(), sizeof(state));
    if (interpreter_->Invoke() != kTfLiteOk) {
      throw std::runtime_error("reference Invoke failed");
    }

    ReferenceOutputs outputs;
    CopyOutput(0, outputs.output0);
    CopyOutput(1, outputs.output1);
    CopyOutput(2, outputs.output2);
    CopyOutput(3, outputs.output3);
    CopyOutput(4, outputs.output4);
    CopyOutput(5, outputs.output5);
    CopyOutput(6, outputs.output6);
    return outputs;
  }

  [[nodiscard]] const tflite::Model* SchemaModel() const { return model_->GetModel(); }
  [[nodiscard]] const tflite::Interpreter& Interpreter() const { return *interpreter_; }

 private:
  template <typename Container>
  void CopyOutput(int index, Container& destination) {
    const float* source = interpreter_->typed_output_tensor<float>(index);
    if (source == nullptr) throw std::runtime_error("reference output type mismatch");
    std::copy_n(source, destination.size(), destination.begin());
  }

  void ValidateContract() {
    if (interpreter_->inputs().size() != 3 || interpreter_->outputs().size() != 7) {
      throw std::runtime_error("reference tensor count mismatch");
    }
    const std::vector<int> spectral{1, 1, 1024};
    const std::vector<int> state{1, 6, 52, 48};
    RequireFloatTensor(interpreter_->input_tensor(0), spectral, "reference input 0");
    RequireFloatTensor(interpreter_->input_tensor(1), spectral, "reference input 1");
    RequireFloatTensor(interpreter_->input_tensor(2), state, "reference input 2");
    for (int i = 0; i < 7; ++i) {
      RequireFloatTensor(interpreter_->output_tensor(i), i == 2 ? state : spectral,
                         "reference output " + std::to_string(i));
    }
  }

  tflite::ops::builtin::BuiltinRefOpResolver resolver_;
  std::vector<std::uint8_t> model_bytes_;
  std::unique_ptr<tflite::FlatBufferModel> model_;
  std::unique_ptr<tflite::Interpreter> interpreter_;
};

template <typename Container>
void WriteFloatFile(const std::filesystem::path& path, const Container& values) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("cannot create " + path.string());
  stream.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(float)));
  if (!stream) throw std::runtime_error("cannot write " + path.string());
}

template <typename Callable>
double Milliseconds(Callable&& callable) {
  const auto start = std::chrono::steady_clock::now();
  callable();
  const auto finish = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

void AddMetric(std::ofstream& csv, std::ostringstream& report,
               const std::string& test_case, const std::string& tensor,
               const Metrics& metrics) {
  csv << test_case << ',' << tensor << ',' << std::setprecision(12) << metrics.max_abs
      << ',' << metrics.rms << ',' << metrics.reference_max_abs << ','
      << metrics.reference_rms << ',' << metrics.max_abs_ratio << ','
      << metrics.rms_ratio << ',' << metrics.max_element_bound_ratio << ','
      << metrics.worst_index << '\n';
  report << "  " << test_case << " / " << tensor << ": max_abs="
         << std::setprecision(9) << metrics.max_abs << ", rms=" << metrics.rms
         << ", max/ref_max=" << metrics.max_abs_ratio
         << ", rms/ref_rms=" << metrics.rms_ratio
         << ", max_element_bound_ratio=" << metrics.max_element_bound_ratio
         << ", worst_index=" << metrics.worst_index << '\n';
}

bool WithinReferenceTolerance(const Metrics& metrics) {
  // The reference resolver intentionally changes arithmetic implementations
  // throughout a 9,133-op recurrent graph. Permit bounded operation-order drift
  // while still rejecting materially different source heads or state evolution.
  constexpr double kMaxAbsRatioLimit = 0.01;  // 1% of reference peak magnitude.
  constexpr double kRmsRatioLimit = 0.01;     // 1% of reference RMS magnitude.
  return metrics.max_abs_ratio <= kMaxAbsRatioLimit &&
         metrics.rms_ratio <= kRmsRatioLimit &&
         metrics.max_element_bound_ratio <= 1.0;
}

std::map<std::string, int> OperatorCounts(const tflite::Model* model) {
  std::map<std::string, int> counts;
  if (model == nullptr || model->subgraphs() == nullptr || model->subgraphs()->size() != 1) {
    throw std::runtime_error("expected exactly one model subgraph");
  }
  const auto* codes = model->operator_codes();
  const auto* operators = model->subgraphs()->Get(0)->operators();
  for (const auto* op : *operators) {
    const auto* code = codes->Get(op->opcode_index());
    const auto builtin = tflite::GetBuiltinCode(code);
    ++counts[tflite::EnumNameBuiltinOperator(builtin)];
  }
  return counts;
}

void WriteReferenceVectors(const std::filesystem::path& directory,
                           const Spectral& first_left, const Spectral& first_right,
                           const ReferenceOutputs& first,
                           const Spectral& second_left, const Spectral& second_right,
                           const ReferenceOutputs& second) {
  const auto vectors = directory / "reference-vectors";
  std::filesystem::create_directories(vectors);
  const State zero_state{};

  WriteFloatFile(vectors / "first-input-left.f32le", first_left);
  WriteFloatFile(vectors / "first-input-right.f32le", first_right);
  WriteFloatFile(vectors / "first-input-state-zero.f32le", zero_state);
  WriteFloatFile(vectors / "first-output-0-voice-left.f32le", first.output0);
  WriteFloatFile(vectors / "first-output-1-voice-right.f32le", first.output1);
  WriteFloatFile(vectors / "first-output-2-state.f32le", first.output2);
  WriteFloatFile(vectors / "first-output-3-music-left.f32le", first.output3);
  WriteFloatFile(vectors / "first-output-4-music-right.f32le", first.output4);
  WriteFloatFile(vectors / "first-output-5-unused.f32le", first.output5);
  WriteFloatFile(vectors / "first-output-6-unused.f32le", first.output6);

  WriteFloatFile(vectors / "recurrent-input-left.f32le", second_left);
  WriteFloatFile(vectors / "recurrent-input-right.f32le", second_right);
  WriteFloatFile(vectors / "recurrent-input-state.f32le", first.output2);
  WriteFloatFile(vectors / "recurrent-output-0-voice-left.f32le", second.output0);
  WriteFloatFile(vectors / "recurrent-output-1-voice-right.f32le", second.output1);
  WriteFloatFile(vectors / "recurrent-output-2-state.f32le", second.output2);
  WriteFloatFile(vectors / "recurrent-output-3-music-left.f32le", second.output3);
  WriteFloatFile(vectors / "recurrent-output-4-music-right.f32le", second.output4);
  WriteFloatFile(vectors / "recurrent-output-5-unused.f32le", second.output5);
  WriteFloatFile(vectors / "recurrent-output-6-unused.f32le", second.output6);

  const std::array<std::string, 20> expected_names = {
      "first-input-left.f32le",
      "first-input-right.f32le",
      "first-input-state-zero.f32le",
      "first-output-0-voice-left.f32le",
      "first-output-1-voice-right.f32le",
      "first-output-2-state.f32le",
      "first-output-3-music-left.f32le",
      "first-output-4-music-right.f32le",
      "first-output-5-unused.f32le",
      "first-output-6-unused.f32le",
      "recurrent-input-left.f32le",
      "recurrent-input-right.f32le",
      "recurrent-input-state.f32le",
      "recurrent-output-0-voice-left.f32le",
      "recurrent-output-1-voice-right.f32le",
      "recurrent-output-2-state.f32le",
      "recurrent-output-3-music-left.f32le",
      "recurrent-output-4-music-right.f32le",
      "recurrent-output-5-unused.f32le",
      "recurrent-output-6-unused.f32le"};

  for (const auto& entry : std::filesystem::directory_iterator(vectors)) {
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    if (std::find(expected_names.begin(), expected_names.end(), name) ==
        expected_names.end()) {
      throw std::runtime_error("unexpected stale reference vector: " + name);
    }
  }

  std::ofstream manifest(directory / "reference-vector-manifest.csv", std::ios::trunc);
  if (!manifest) throw std::runtime_error("cannot create reference-vector-manifest.csv");
  manifest << "file,case,role,output_index,shape,elements,bytes,producer\n";
  for (const auto& name : expected_names) {
    const auto path = vectors / name;
    const bool state = name.find("state") != std::string::npos;
    const bool recurrent = name.rfind("recurrent-", 0) == 0;
    std::string role = name.find("input") != std::string::npos ? "input" : "output";
    std::string index;
    const auto output_marker = name.find("output-");
    if (output_marker != std::string::npos) {
      index = name.substr(output_marker + 7, 1);
    }
    manifest << "reference-vectors/" << name << ','
             << (recurrent ? "recurrent" : "first") << ',' << role << ',' << index
             << ',' << (state ? "[1;6;52;48]" : "[1;1;1024]") << ','
             << (state ? Model::kStateFloats : Model::kSpectralFloats) << ','
             << std::filesystem::file_size(path) << ",BuiltinRefOpResolver\n";
  }
}

int Run(const std::filesystem::path& model_path,
        const std::filesystem::path& evidence_directory) {
  std::filesystem::create_directories(evidence_directory);
  std::ofstream csv(evidence_directory / "numerical-results.csv", std::ios::trunc);
  if (!csv) throw std::runtime_error("cannot create numerical-results.csv");
  csv << "case,tensor,max_abs_error,rms_error,reference_max_abs,reference_rms,"
         "max_abs_ratio,rms_ratio,max_element_bound_ratio,worst_index\n";

  ReferenceInterpreter reference(model_path);
  Model production(model_path);

  std::ostringstream report;
  report << "CleanWave Step 3 model verification\n";
  report << "TensorFlow Lite runtime: " << TfLiteVersion() << "\n";
  report << "Production path: stable TFLite C API, 1 thread, no delegate added; "
            "TFLite CreateOpResolver uses BuiltinOpResolverWithoutDefaultDelegates\n";
  report << "Independent reference path: BuiltinRefOpResolver, 1 thread\n";
  report << "Model: " << model_path.string() << "\n\n";

  const auto& interpreter = reference.Interpreter();
  report << "Tensor contract\n";
  report << "  inputs=" << interpreter.inputs().size() << ", outputs="
         << interpreter.outputs().size() << "\n";
  for (std::size_t i = 0; i < interpreter.inputs().size(); ++i) {
    const auto* tensor = interpreter.input_tensor(i);
    report << "  input " << i << ": name=" << (tensor->name ? tensor->name : "")
           << ", type=FLOAT32, shape=" << ShapeString(tensor)
           << ", bytes=" << tensor->bytes << "\n";
  }
  for (std::size_t i = 0; i < interpreter.outputs().size(); ++i) {
    const auto* tensor = interpreter.output_tensor(i);
    report << "  output " << i << ": name=" << (tensor->name ? tensor->name : "")
           << ", type=FLOAT32, shape=" << ShapeString(tensor)
           << ", bytes=" << tensor->bytes;
    if (i == 0) report << ", semantic=voice_left";
    if (i == 1) report << ", semantic=voice_right";
    if (i == 2) report << ", semantic=next_state";
    if (i == 3) report << ", semantic=music_left";
    if (i == 4) report << ", semantic=music_right";
    if (i >= 5) report << ", semantic=unused_by_current_wrapper";
    report << "\n";
  }

  const auto op_counts = OperatorCounts(reference.SchemaModel());
  int op_total = 0;
  report << "\nOperator compatibility\n";
  for (const auto& [name, count] : op_counts) {
    report << "  " << name << '=' << count << "\n";
    op_total += count;
  }
  report << "  total=" << op_total << "\n";
  report << "  production_allocate=PASS, reference_allocate=PASS\n";

  const Spectral first_left = MakeInput(0x13579BDFU);
  const Spectral first_right = MakeInput(0x2468ACE0U);
  const Spectral second_left = MakeInput(0x0BADF00DU);
  const Spectral second_right = MakeInput(0xC001D00DU);
  const State zero_state{};

  ReferenceOutputs ref_first;
  ReferenceOutputs ref_second;
  ReferenceOutputs ref_second_production_state;
  ReferenceOutputs ref_second_zero_state;
  const double reference_first_ms =
      Milliseconds([&] { ref_first = reference.Run(first_left, first_right, zero_state); });
  const double reference_second_ms = Milliseconds(
      [&] { ref_second = reference.Run(second_left, second_right, ref_first.output2); });
  const double reference_zero_state_second_ms = Milliseconds(
      [&] { ref_second_zero_state = reference.Run(second_left, second_right, zero_state); });

  Model::SourceHeads prod_first;
  Model::SourceHeads prod_second;
  double production_first_ms = Milliseconds(
      [&] { production.Process(first_left, first_right, prod_first); });
  const State prod_first_state = production.StateSnapshot();
  double production_second_ms = Milliseconds(
      [&] { production.Process(second_left, second_right, prod_second); });
  const State prod_second_state = production.StateSnapshot();
  const double reference_production_state_second_ms = Milliseconds([&] {
    ref_second_production_state =
        reference.Run(second_left, second_right, prod_first_state);
  });

  report << "\nOptimized-C-API vs reference-kernel same-state numerical error\n";
  bool reference_parity_pass = true;
  const auto parity_metric = [&](const std::string& test_case,
                                 const std::string& tensor, const auto& actual,
                                 const auto& expected) {
    const Metrics metrics = Compare(actual, expected);
    AddMetric(csv, report, test_case, tensor, metrics);
    reference_parity_pass = reference_parity_pass && WithinReferenceTolerance(metrics);
  };
  parity_metric("first", "voice_left", prod_first.voice_left, ref_first.output0);
  parity_metric("first", "voice_right", prod_first.voice_right, ref_first.output1);
  parity_metric("first", "state", prod_first_state, ref_first.output2);
  parity_metric("first", "music_left", prod_first.music_left, ref_first.output3);
  parity_metric("first", "music_right", prod_first.music_right, ref_first.output4);
  parity_metric("recurrent_same_state", "voice_left", prod_second.voice_left,
                ref_second_production_state.output0);
  parity_metric("recurrent_same_state", "voice_right", prod_second.voice_right,
                ref_second_production_state.output1);
  parity_metric("recurrent_same_state", "state", prod_second_state,
                ref_second_production_state.output2);
  parity_metric("recurrent_same_state", "music_left", prod_second.music_left,
                ref_second_production_state.output3);
  parity_metric("recurrent_same_state", "music_right", prod_second.music_right,
                ref_second_production_state.output4);
  report << "  tolerance=max_abs<=1% reference peak AND rms<=1% reference RMS AND "
            "each element <= 5e-4 + 1%*abs(reference): "
         << (reference_parity_pass ? "PASS" : "FAIL") << "\n";

  report << "\nIndependent recurrent-trajectory drift (each resolver feeds back its own state)\n";
  AddMetric(csv, report, "recurrent_trajectory", "voice_left",
            Compare(prod_second.voice_left, ref_second.output0));
  AddMetric(csv, report, "recurrent_trajectory", "voice_right",
            Compare(prod_second.voice_right, ref_second.output1));
  AddMetric(csv, report, "recurrent_trajectory", "state",
            Compare(prod_second_state, ref_second.output2));
  AddMetric(csv, report, "recurrent_trajectory", "music_left",
            Compare(prod_second.music_left, ref_second.output3));
  AddMetric(csv, report, "recurrent_trajectory", "music_right",
            Compare(prod_second.music_right, ref_second.output4));
  report << "  This trajectory comparison is reported as measured runtime drift, not used "
            "as the same-state kernel acceptance gate.\n";

  report << "\nRecurrent-state effect in independent reference path\n";
  const Metrics state_effect_voice_left = Compare(ref_second.output0, ref_second_zero_state.output0);
  const Metrics state_effect_voice_right = Compare(ref_second.output1, ref_second_zero_state.output1);
  const Metrics state_effect_state = Compare(ref_second.output2, ref_second_zero_state.output2);
  const Metrics state_effect_music_left = Compare(ref_second.output3, ref_second_zero_state.output3);
  const Metrics state_effect_music_right = Compare(ref_second.output4, ref_second_zero_state.output4);
  report << "  voice_left_max_delta=" << state_effect_voice_left.max_abs << "\n";
  report << "  voice_right_max_delta=" << state_effect_voice_right.max_abs << "\n";
  report << "  state_max_delta=" << state_effect_state.max_abs << "\n";
  report << "  music_left_max_delta=" << state_effect_music_left.max_abs << "\n";
  report << "  music_right_max_delta=" << state_effect_music_right.max_abs << "\n";
  const bool recurrent_state_has_effect =
      std::max({state_effect_voice_left.max_abs, state_effect_voice_right.max_abs,
                state_effect_state.max_abs, state_effect_music_left.max_abs,
                state_effect_music_right.max_abs}) > 1.0e-6;
  report << "  recurrent_state_effect="
         << (recurrent_state_has_effect ? "PASS" : "FAIL") << "\n";

  production.Reset();
  const State reset_zero = production.StateSnapshot();
  Model::SourceHeads prod_reset;
  production.Process(first_left, first_right, prod_reset);
  const State prod_reset_state = production.StateSnapshot();
  report << "\nReset reproducibility\n";
  report << "  state_immediately_after_reset_max_abs=" << MaxAbs(reset_zero) << "\n";
  AddMetric(csv, report, "reset_vs_initial", "voice_left",
            Compare(prod_reset.voice_left, prod_first.voice_left));
  AddMetric(csv, report, "reset_vs_initial", "voice_right",
            Compare(prod_reset.voice_right, prod_first.voice_right));
  AddMetric(csv, report, "reset_vs_initial", "state",
            Compare(prod_reset_state, prod_first_state));
  AddMetric(csv, report, "reset_vs_initial", "music_left",
            Compare(prod_reset.music_left, prod_first.music_left));
  AddMetric(csv, report, "reset_vs_initial", "music_right",
            Compare(prod_reset.music_right, prod_first.music_right));

  Model independent(model_path);
  Model::SourceHeads independent_first;
  Model::SourceHeads independent_second;
  independent.Process(first_left, first_right, independent_first);
  independent.Process(second_left, second_right, independent_second);
  const State independent_state = independent.StateSnapshot();
  report << "\nIndependent stream isolation\n";
  AddMetric(csv, report, "independent_stream", "voice_left",
            Compare(independent_second.voice_left, prod_second.voice_left));
  AddMetric(csv, report, "independent_stream", "voice_right",
            Compare(independent_second.voice_right, prod_second.voice_right));
  AddMetric(csv, report, "independent_stream", "state",
            Compare(independent_state, prod_second_state));
  AddMetric(csv, report, "independent_stream", "music_left",
            Compare(independent_second.music_left, prod_second.music_left));
  AddMetric(csv, report, "independent_stream", "music_right",
            Compare(independent_second.music_right, prod_second.music_right));

  report << "\nInvocation timing (single observations; model-only, no DSP/audio)\n";
  report << std::fixed << std::setprecision(3);
  report << "  production_first_ms=" << production_first_ms << "\n";
  report << "  production_recurrent_ms=" << production_second_ms << "\n";
  report << "  reference_first_ms=" << reference_first_ms << "\n";
  report << "  reference_recurrent_ms=" << reference_second_ms << "\n";
  report << "  reference_production_state_second_ms="
         << reference_production_state_second_ms << "\n";
  report << "  reference_zero_state_second_ms=" << reference_zero_state_second_ms << "\n";

  WriteReferenceVectors(evidence_directory, first_left, first_right, ref_first,
                        second_left, second_right, ref_second);
  report << "\nReference vectors: "
         << (evidence_directory / "reference-vectors").string() << "\n";
  const bool reset_pass = MaxAbs(reset_zero) == 0.0 &&
                          Compare(prod_reset.voice_left, prod_first.voice_left).max_abs == 0.0 &&
                          Compare(prod_reset.voice_right, prod_first.voice_right).max_abs == 0.0 &&
                          Compare(prod_reset_state, prod_first_state).max_abs == 0.0 &&
                          Compare(prod_reset.music_left, prod_first.music_left).max_abs == 0.0 &&
                          Compare(prod_reset.music_right, prod_first.music_right).max_abs == 0.0;
  const bool isolation_pass =
      Compare(independent_second.voice_left, prod_second.voice_left).max_abs == 0.0 &&
      Compare(independent_second.voice_right, prod_second.voice_right).max_abs == 0.0 &&
      Compare(independent_state, prod_second_state).max_abs == 0.0 &&
      Compare(independent_second.music_left, prod_second.music_left).max_abs == 0.0 &&
      Compare(independent_second.music_right, prod_second.music_right).max_abs == 0.0;
  const bool pass = reference_parity_pass && recurrent_state_has_effect && reset_pass && isolation_pass;
  report << "RESULT=" << (pass ? "PASS" : "FAIL") << "\n";

  const std::string report_text = report.str();
  std::ofstream verification(evidence_directory / "model-verification.txt",
                             std::ios::trunc);
  if (!verification) throw std::runtime_error("cannot create model-verification.txt");
  verification << report_text;
  std::cout << report_text;
  return pass ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      std::cerr << "Usage: cleanwave_model_verify <model.tflite> <evidence-directory>\n";
      return 2;
    }
    return Run(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
  } catch (const std::exception& error) {
    std::cerr << "Step 3 verification failed: " << error.what() << '\n';
    return 1;
  }
}
