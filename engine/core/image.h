#pragma once
// Images: float RGBA in memory, PNG in and out (our own deflate/inflate),
// PFM (float) out. No outside libraries.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math.h"

namespace loop {

struct Image {
  int width = 0, height = 0;
  std::vector<Vec4> px;  // linear values, row 0 at the top
  Image() = default;
  Image(int w, int h, Vec4 fill = {0, 0, 0, 1}) : width(w), height(h), px(static_cast<size_t>(w) * h, fill) {}
  Vec4& at(int x, int y) { return px[static_cast<size_t>(y) * width + x]; }
  const Vec4& at(int x, int y) const { return px[static_cast<size_t>(y) * width + x]; }
  bool empty() const { return px.empty(); }
  // Bilinear sample with wrap (u, v in 0..1, v down).
  Vec4 sample(float u, float v) const;
};

// An 8-bit RGBA picture as stored (sRGB or plain numbers, the user decides),
// for model textures: a quarter of an Image's memory.
struct Texture {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;  // row 0 at the top
  // The picture's file (JPEG or PNG) while it waits to be decoded: a scene's
  // pictures are decoded all together, on every core (decodeTextures).
  std::vector<uint8_t> packed;
  bool pending() const { return !packed.empty(); }
  bool empty() const { return rgba.empty(); }
  // Bilinear sample with wrap (u, v in 0..1, v down). `srgb`: the colour
  // channels are sRGB-encoded and come back linear; alpha is always plain.
  Vec4 sample(float u, float v, bool srgb) const;
};

// Decodes every waiting picture (Texture::packed) at once, spread over every
// core; each is left empty (and counted) if it can't be read. Returns how many
// couldn't be; `error` gets the first reason.
int decodeTextures(std::vector<Texture>& textures, std::string* error = nullptr);

// sRGB transfer.
float linearToSrgb(float v);
float srgbToLinear(float v);

// zlib streams (RFC 1950/1951): compress with LZ77 + fixed Huffman,
// decompress all block types.
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t size);
bool zlibDecompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

// 8-bit RGBA PNG from bytes.
std::vector<uint8_t> encodePng(const uint8_t* rgba, int w, int h);
bool decodePng(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h);

// `img` is already display-ready (0..1, sRGB-encoded) when `encoded`;
// otherwise it's linear and gets the sRGB curve.
bool writePng(const std::string& path, const Image& img, bool encoded = false);
bool readPng(const std::string& path, Image& img, bool toLinear = true);
bool writePfm(const std::string& path, const Image& img);

}  // namespace loop
