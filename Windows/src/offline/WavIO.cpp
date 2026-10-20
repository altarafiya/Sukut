#include "offline/WavIO.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace cleanwave {
namespace {

std::uint16_t ReadU16(std::istream& stream) {
  std::array<unsigned char, 2> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), 2);
  if (!stream) throw std::runtime_error("truncated WAV file");
  return static_cast<std::uint16_t>(bytes[0]) |
         (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t ReadU32(std::istream& stream) {
  std::array<unsigned char, 4> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), 4);
  if (!stream) throw std::runtime_error("truncated WAV file");
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void WriteU16(std::ostream& stream, std::uint16_t value) {
  const std::array<unsigned char, 2> bytes = {
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU)};
  stream.write(reinterpret_cast<const char*>(bytes.data()), 2);
}

void WriteU32(std::ostream& stream, std::uint32_t value) {
  const std::array<unsigned char, 4> bytes = {
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU),
      static_cast<unsigned char>((value >> 16U) & 0xffU),
      static_cast<unsigned char>((value >> 24U) & 0xffU)};
  stream.write(reinterpret_cast<const char*>(bytes.data()), 4);
}

bool FourCcEquals(const std::array<char, 4>& value, const char* expected) {
  return std::memcmp(value.data(), expected, 4) == 0;
}

}  // namespace

WavPcm16Stereo ReadPcm16StereoWav(const std::filesystem::path& path) {
  const std::uint64_t file_size = std::filesystem::file_size(path);
  if (file_size < 12) throw std::runtime_error("WAV file is too small");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("unable to open input WAV");

  std::array<char, 4> riff{};
  stream.read(riff.data(), 4);
  const std::uint32_t riff_size = ReadU32(stream);
  std::array<char, 4> wave{};
  stream.read(wave.data(), 4);
  if (!stream || !FourCcEquals(riff, "RIFF") || !FourCcEquals(wave, "WAVE")) {
    throw std::runtime_error("input is not a RIFF/WAVE file");
  }
  const std::uint64_t riff_end = 8ULL + riff_size;
  if (riff_end < 12 || riff_end > file_size) {
    throw std::runtime_error("WAV RIFF size exceeds file bounds");
  }

  bool have_fmt = false;
  bool have_data = false;
  std::uint16_t format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t byte_rate = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits_per_sample = 0;
  std::vector<std::uint8_t> data;

  while (stream && !(have_fmt && have_data)) {
    const auto header_pos = stream.tellg();
    if (header_pos < 0 || static_cast<std::uint64_t>(header_pos) + 8ULL > riff_end) {
      break;
    }
    std::array<char, 4> id{};
    stream.read(id.data(), 4);
    if (stream.gcount() == 0) break;
    if (!stream) throw std::runtime_error("truncated WAV chunk header");
    const std::uint32_t size = ReadU32(stream);
    const auto payload_pos = stream.tellg();
    if (payload_pos < 0) throw std::runtime_error("invalid WAV chunk position");
    const std::uint64_t padded_size = static_cast<std::uint64_t>(size) + (size & 1U);
    const std::uint64_t payload_end =
        static_cast<std::uint64_t>(payload_pos) + padded_size;
    if (payload_end > riff_end) {
      throw std::runtime_error("WAV chunk exceeds RIFF bounds");
    }
    if (FourCcEquals(id, "fmt ")) {
      if (size < 16) throw std::runtime_error("invalid WAV fmt chunk");
      format = ReadU16(stream);
      channels = ReadU16(stream);
      sample_rate = ReadU32(stream);
      byte_rate = ReadU32(stream);
      block_align = ReadU16(stream);
      bits_per_sample = ReadU16(stream);
      have_fmt = true;
    } else if (FourCcEquals(id, "data")) {
      data.resize(size);
      if (size != 0) stream.read(reinterpret_cast<char*>(data.data()), size);
      if (!stream) throw std::runtime_error("truncated WAV data chunk");
      have_data = true;
    }
    stream.seekg(static_cast<std::streamoff>(payload_end), std::ios::beg);
  }

  if (!have_fmt || !have_data) throw std::runtime_error("WAV lacks fmt or data chunk");
  if (format != 1 || channels != 2 || sample_rate != 48000 || bits_per_sample != 16) {
    throw std::runtime_error("WAV must be PCM16 stereo at 48000 Hz");
  }
  if (block_align != 4 || byte_rate != sample_rate * block_align) {
    throw std::runtime_error("WAV byte rate/block alignment is inconsistent");
  }
  if ((data.size() % 4) != 0) throw std::runtime_error("WAV data is not frame-aligned");

  WavPcm16Stereo wav;
  wav.sample_rate = sample_rate;
  wav.samples.resize(data.size() / 2);
  for (std::size_t i = 0; i < wav.samples.size(); ++i) {
    const std::uint16_t packed = static_cast<std::uint16_t>(data[i * 2]) |
                                 (static_cast<std::uint16_t>(data[i * 2 + 1]) << 8U);
    wav.samples[i] = static_cast<std::int16_t>(packed);
  }
  return wav;
}

void WritePcm16StereoWav(const std::filesystem::path& path,
                         const WavPcm16Stereo& wav) {
  if (wav.sample_rate != 48000 || (wav.samples.size() % 2) != 0) {
    throw std::runtime_error("output WAV must be 48000 Hz stereo PCM16");
  }
  const std::uint64_t data_bytes_64 = wav.samples.size() * sizeof(std::int16_t);
  if (data_bytes_64 > 0xffffffffULL - 36ULL) {
    throw std::runtime_error("WAV exceeds RIFF 32-bit size limit");
  }
  const auto data_bytes = static_cast<std::uint32_t>(data_bytes_64);

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) throw std::runtime_error("unable to create output WAV");
  stream.write("RIFF", 4);
  WriteU32(stream, 36U + data_bytes);
  stream.write("WAVEfmt ", 8);
  WriteU32(stream, 16);
  WriteU16(stream, 1);
  WriteU16(stream, 2);
  WriteU32(stream, wav.sample_rate);
  WriteU32(stream, wav.sample_rate * 4U);
  WriteU16(stream, 4);
  WriteU16(stream, 16);
  stream.write("data", 4);
  WriteU32(stream, data_bytes);
  for (const std::int16_t sample : wav.samples) {
    WriteU16(stream, static_cast<std::uint16_t>(sample));
  }
  if (!stream) throw std::runtime_error("failed while writing output WAV");
}

}  // namespace cleanwave
