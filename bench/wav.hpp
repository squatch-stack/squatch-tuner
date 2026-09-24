// SPDX-License-Identifier: MIT
// Minimal WAV reader for the bench (not the audio path): PCM 16/24/32 and float32,
// first channel only.
#pragma once
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace bench {

struct Audio {
  std::vector<float> x;
  float rate = 48000.0f;
};

namespace wavdetail {
inline uint32_t u32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
inline uint16_t u16(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }

inline float sampleAt(const unsigned char* p, int bits, bool isFloat) {
  if (isFloat) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
  }
  if (bits == 16) return static_cast<float>(int16_t(u16(p))) / 32768.0f;
  if (bits == 24) {
    int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
    if (v & 0x800000) v -= 0x1000000;
    return static_cast<float>(v) / 8388608.0f;
  }
  return static_cast<float>(int32_t(u32(p))) / 2147483648.0f;
}

inline std::vector<unsigned char> slurp(const std::string& path) {
  std::vector<unsigned char> buf;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return buf;
  std::fseek(f, 0, SEEK_END);
  buf.resize(static_cast<size_t>(std::ftell(f)));
  std::fseek(f, 0, SEEK_SET);
  if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
  std::fclose(f);
  return buf;
}
}  // namespace wavdetail

namespace wavdetail {
struct Format {
  int channels = 0, bits = 0;
  bool isFloat = false;
};

inline Format parseFmt(const unsigned char* d, uint32_t len, Audio& out) {
  Format f;
  const uint16_t tag = u16(d);
  f.channels = u16(d + 2);
  out.rate = static_cast<float>(u32(d + 4));
  f.bits = u16(d + 14);
  f.isFloat = tag == 3 || (tag == 0xFFFE && len >= 26 && u16(d + 24) == 3);
  return f;
}

inline void decode(const unsigned char* d, size_t bytes, const Format& f, Audio& out) {
  const size_t frame = static_cast<size_t>(f.channels) * static_cast<size_t>(f.bits / 8);
  out.x.resize(bytes / frame);
  for (size_t i = 0; i < out.x.size(); ++i) out.x[i] = sampleAt(d + i * frame, f.bits, f.isFloat);
}
}  // namespace wavdetail

/// Returns false if the file is missing or not a supported WAV. Keeps the first channel.
inline bool readWav(const std::string& path, Audio& out) {
  using namespace wavdetail;
  const std::vector<unsigned char> b = slurp(path);
  if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
  Format f;
  for (size_t pos = 12; pos + 8 <= b.size();) {
    const uint32_t len = u32(&b[pos + 4]);
    const unsigned char* d = &b[pos + 8];
    if (!std::memcmp(&b[pos], "fmt ", 4)) f = parseFmt(d, len, out);
    if (!std::memcmp(&b[pos], "data", 4) && f.channels > 0 && f.bits >= 16) {
      decode(d, std::min<size_t>(len, b.size() - pos - 8), f, out);
      return true;
    }
    pos += 8 + len + (len & 1);
  }
  return false;
}

}  // namespace bench
