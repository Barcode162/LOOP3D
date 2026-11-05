#include "engine/core/image.h"

#include <cstdio>
#include <cstring>

#include "engine/core/file.h"

namespace loop {

Vec4 Image::sample(float u, float v) const {
  if (empty()) return {};
  float fx = u * width - 0.5f, fy = v * height - 0.5f;
  int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  float tx = fx - x0, ty = fy - y0;
  auto wrap = [](int i, int n) { i %= n; return i < 0 ? i + n : i; };
  int xa = wrap(x0, width), xb = wrap(x0 + 1, width), ya = wrap(y0, height), yb = wrap(y0 + 1, height);
  Vec4 a = at(xa, ya) * (1 - tx) + at(xb, ya) * tx;
  Vec4 b = at(xa, yb) * (1 - tx) + at(xb, yb) * tx;
  return a * (1 - ty) + b * ty;
}

Vec4 Texture::sample(float u, float v, bool srgb) const {
  if (empty()) return {1, 1, 1, 1};
  static float lut[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; ++i) lut[i] = srgbToLinear(i / 255.0f);
    ready = true;
  }
  float fx = u * width - 0.5f, fy = v * height - 0.5f;
  if (!std::isfinite(fx) || !std::isfinite(fy)) fx = fy = 0;
  int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
  float tx = fx - x0, ty = fy - y0;
  auto wrap = [](int i, int n) { i %= n; return i < 0 ? i + n : i; };
  int xa = wrap(x0, width), xb = wrap(x0 + 1, width), ya = wrap(y0, height), yb = wrap(y0 + 1, height);
  auto px = [&](int x, int y) {
    const uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
    if (srgb) return Vec4(lut[p[0]], lut[p[1]], lut[p[2]], p[3] / 255.0f);
    return Vec4(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f);
  };
  Vec4 a = px(xa, ya) * (1 - tx) + px(xb, ya) * tx;
  Vec4 b = px(xa, yb) * (1 - tx) + px(xb, yb) * tx;
  return a * (1 - ty) + b * ty;
}

float linearToSrgb(float v) {
  v = saturate(v);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
}
float srgbToLinear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }

// ---------------------------------------------------------------- checksums
uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320U ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}
static uint32_t adler32(const uint8_t* d, size_t n) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < n; ++i) a = (a + d[i]) % 65521, b = (b + a) % 65521;
  return (b << 16) | a;
}

// ---------------------------------------------------------------- deflate
namespace {

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct BitWriter {
  std::vector<uint8_t>& out;
  uint32_t buf = 0;
  int n = 0;
  void bits(uint32_t v, int count) {  // LSB first
    buf |= v << n;
    n += count;
    while (n >= 8) out.push_back(static_cast<uint8_t>(buf)), buf >>= 8, n -= 8;
  }
  void huff(uint32_t code, int len) {  // Huffman codes go MSB first
    uint32_t r = 0;
    for (int i = 0; i < len; ++i) r |= ((code >> i) & 1) << (len - 1 - i);
    bits(r, len);
  }
  void flush() {
    if (n > 0) out.push_back(static_cast<uint8_t>(buf)), buf = 0, n = 0;
  }
};

void fixedLit(BitWriter& w, int sym) {
  if (sym < 144) w.huff(0x30 + sym, 8);
  else if (sym < 256) w.huff(0x190 + sym - 144, 9);
  else if (sym < 280) w.huff(sym - 256, 7);
  else w.huff(0xc0 + sym - 280, 8);
}

}  // namespace

std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t size) {
  std::vector<uint8_t> out = {0x78, 0x01};
  BitWriter w{out, 0, 0};
  w.bits(1, 1);  // final block
  w.bits(1, 2);  // fixed Huffman
  constexpr int kHashBits = 15, kWindow = 32768, kMaxChain = 48;
  std::vector<int> head(1 << kHashBits, -1), prev(size, -1);
  auto hashAt = [&](size_t i) { return ((data[i] << 10) ^ (data[i + 1] << 5) ^ data[i + 2]) & ((1 << kHashBits) - 1); };
  size_t i = 0;
  while (i < size) {
    int bestLen = 0, bestDist = 0;
    if (i + 2 < size) {
      int h = hashAt(i);
      int cand = head[h], chain = 0;
      while (cand >= 0 && static_cast<int>(i) - cand <= kWindow && chain++ < kMaxChain) {
        int len = 0, maxLen = static_cast<int>(std::min<size_t>(258, size - i));
        while (len < maxLen && data[cand + len] == data[i + len]) ++len;
        if (len > bestLen) bestLen = len, bestDist = static_cast<int>(i) - cand;
        if (len == maxLen) break;
        cand = prev[cand];
      }
      prev[i] = head[h];
      head[h] = static_cast<int>(i);
    }
    if (bestLen >= 3) {
      int li = 0;
      while (li < 28 && kLenBase[li + 1] <= bestLen) ++li;
      fixedLit(w, 257 + li);
      w.bits(bestLen - kLenBase[li], kLenExtra[li]);
      int di = 0;
      while (di < 29 && kDistBase[di + 1] <= bestDist) ++di;
      w.huff(di, 5);
      w.bits(bestDist - kDistBase[di], kDistExtra[di]);
      for (int k = 1; k < bestLen; ++k) {
        size_t j = i + k;
        if (j + 2 < size) {
          int h = hashAt(j);
          prev[j] = head[h];
          head[h] = static_cast<int>(j);
        }
      }
      i += bestLen;
    } else {
      fixedLit(w, data[i]);
      ++i;
    }
  }
  fixedLit(w, 256);
  w.flush();
  uint32_t a = adler32(data, size);
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(a >> s));
  return out;
}

// ---------------------------------------------------------------- inflate
namespace {

struct BitReader {
  const uint8_t* d;
  size_t n, pos = 0;
  uint32_t buf = 0;
  int cnt = 0;
  bool bad = false;
  uint32_t bits(int k) {
    while (cnt < k) {
      if (pos >= n) { bad = true; return 0; }
      buf |= static_cast<uint32_t>(d[pos++]) << cnt;
      cnt += 8;
    }
    uint32_t v = buf & ((1u << k) - 1);
    buf >>= k;
    cnt -= k;
    return v;
  }
  void align() { buf = 0, cnt = 0; }
};

struct Huffman {
  uint16_t counts[16] = {};
  uint16_t symbols[320] = {};
  bool build(const uint8_t* lengths, int n) {
    std::memset(counts, 0, sizeof counts);
    for (int i = 0; i < n; ++i) counts[lengths[i]]++;
    counts[0] = 0;
    uint16_t offs[16] = {};
    for (int i = 1; i < 16; ++i) offs[i] = offs[i - 1] + counts[i - 1];
    for (int i = 0; i < n; ++i)
      if (lengths[i]) symbols[offs[lengths[i]]++] = static_cast<uint16_t>(i);
    return true;
  }
  int decode(BitReader& br) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
      code |= static_cast<int>(br.bits(1));
      int count = counts[len];
      if (code - count < first) return symbols[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
      if (br.bad) return -1;
    }
    return -1;
  }
};

}  // namespace

bool zlibDecompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
  if (size < 6 || (data[0] & 0x0f) != 8) return false;
  BitReader br{data + 2, size - 2, 0, 0, 0, false};
  bool final = false;
  while (!final) {
    final = br.bits(1);
    uint32_t type = br.bits(2);
    if (br.bad) return false;
    if (type == 0) {
      br.align();
      if (br.pos + 4 > br.n) return false;
      uint16_t len = static_cast<uint16_t>(br.d[br.pos] | (br.d[br.pos + 1] << 8));
      br.pos += 4;
      if (br.pos + len > br.n) return false;
      out.insert(out.end(), br.d + br.pos, br.d + br.pos + len);
      br.pos += len;
      continue;
    }
    Huffman lit, dist;
    if (type == 1) {
      uint8_t l[288];
      for (int i = 0; i < 144; ++i) l[i] = 8;
      for (int i = 144; i < 256; ++i) l[i] = 9;
      for (int i = 256; i < 280; ++i) l[i] = 7;
      for (int i = 280; i < 288; ++i) l[i] = 8;
      lit.build(l, 288);
      uint8_t dl[30];
      for (int i = 0; i < 30; ++i) dl[i] = 5;
      dist.build(dl, 30);
    } else if (type == 2) {
      int hlit = static_cast<int>(br.bits(5)) + 257, hdist = static_cast<int>(br.bits(5)) + 1, hclen = static_cast<int>(br.bits(4)) + 4;
      static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t cl[19] = {};
      for (int i = 0; i < hclen; ++i) cl[order[i]] = static_cast<uint8_t>(br.bits(3));
      Huffman clh;
      clh.build(cl, 19);
      uint8_t lens[320] = {};
      int idx = 0;
      while (idx < hlit + hdist) {
        int sym = clh.decode(br);
        if (sym < 0) return false;
        if (sym < 16) lens[idx++] = static_cast<uint8_t>(sym);
        else {
          int rep = 0;
          uint8_t val = 0;
          if (sym == 16) {
            if (idx == 0) return false;
            val = lens[idx - 1];
            rep = 3 + static_cast<int>(br.bits(2));
          } else if (sym == 17) rep = 3 + static_cast<int>(br.bits(3));
          else rep = 11 + static_cast<int>(br.bits(7));
          if (idx + rep > hlit + hdist) return false;
          while (rep--) lens[idx++] = val;
        }
      }
      lit.build(lens, hlit);
      dist.build(lens + hlit, hdist);
    } else return false;
    for (;;) {
      int sym = lit.decode(br);
      if (sym < 0 || br.bad) return false;
      if (sym < 256) out.push_back(static_cast<uint8_t>(sym));
      else if (sym == 256) break;
      else {
        sym -= 257;
        if (sym >= 29) return false;
        int len = kLenBase[sym] + static_cast<int>(br.bits(kLenExtra[sym]));
        int ds = dist.decode(br);
        if (ds < 0 || ds >= 30) return false;
        size_t d = kDistBase[ds] + br.bits(kDistExtra[ds]);
        if (d > out.size()) return false;
        size_t start = out.size() - d;
        for (int k = 0; k < len; ++k) out.push_back(out[start + k]);
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------- PNG
static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}
static void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& body) {
  put32(png, static_cast<uint32_t>(body.size()));
  std::vector<uint8_t> tb(type, type + 4);
  tb.insert(tb.end(), body.begin(), body.end());
  png.insert(png.end(), tb.begin(), tb.end());
  put32(png, crc32(tb.data(), tb.size()));
}

std::vector<uint8_t> encodePng(const uint8_t* rgba, int w, int h) {
  // Filter each row with "Paeth" or "Sub", whichever gives smaller sums.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (w * 4 + 1));
  const size_t stride = static_cast<size_t>(w) * 4;
  std::vector<uint8_t> line(stride);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = rgba + y * stride;
    const uint8_t* up = y > 0 ? row - stride : nullptr;
    int bestF = 0;
    long bestSum = -1;
    for (int f = 0; f < 5; ++f) {
      long sum = 0;
      for (size_t i = 0; i < stride; ++i) {
        int a = i >= 4 ? row[i - 4] : 0, b = up ? up[i] : 0, c = (i >= 4 && up) ? up[i - 4] : 0, x = row[i];
        int p;
        switch (f) {
          case 0: p = x; break;
          case 1: p = x - a; break;
          case 2: p = x - b; break;
          case 3: p = x - ((a + b) >> 1); break;
          default: {
            int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
            p = x - (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
          }
        }
        sum += std::abs(static_cast<int8_t>(p));
      }
      if (bestSum < 0 || sum < bestSum) bestSum = sum, bestF = f;
    }
    raw.push_back(static_cast<uint8_t>(bestF));
    for (size_t i = 0; i < stride; ++i) {
      int a = i >= 4 ? row[i - 4] : 0, b = up ? up[i] : 0, c = (i >= 4 && up) ? up[i - 4] : 0, x = row[i];
      int p;
      switch (bestF) {
        case 0: p = x; break;
        case 1: p = x - a; break;
        case 2: p = x - b; break;
        case 3: p = x - ((a + b) >> 1); break;
        default: {
          int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
          p = x - (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
        }
      }
      raw.push_back(static_cast<uint8_t>(p));
    }
  }
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, static_cast<uint32_t>(w));
  put32(ihdr, static_cast<uint32_t>(h));
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", zlibCompress(raw.data(), raw.size()));
  chunk(png, "IEND", {});
  return png;
}

static uint32_t get32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

bool decodePng(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& w, int& h) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (size < 8 || std::memcmp(data, sig, 8) != 0) return false;
  size_t pos = 8;
  int depth = 0, color = 0, interlace = 0;
  std::vector<uint8_t> idat, palette, trns;
  while (pos + 12 <= size) {
    uint32_t len = get32(data + pos);
    const char* type = reinterpret_cast<const char*>(data + pos + 4);
    const uint8_t* body = data + pos + 8;
    if (pos + 12 + len > size) return false;
    if (!std::memcmp(type, "IHDR", 4)) {
      w = static_cast<int>(get32(body)), h = static_cast<int>(get32(body + 4));
      depth = body[8], color = body[9], interlace = body[12];
    } else if (!std::memcmp(type, "PLTE", 4)) palette.assign(body, body + len);
    else if (!std::memcmp(type, "tRNS", 4)) trns.assign(body, body + len);
    else if (!std::memcmp(type, "IDAT", 4)) idat.insert(idat.end(), body, body + len);
    else if (!std::memcmp(type, "IEND", 4)) break;
    pos += 12 + len;
  }
  if (depth != 8 || interlace != 0 || w <= 0 || h <= 0) return false;  // 8-bit, non-interlaced
  int channels = color == 0 ? 1 : color == 2 ? 3 : color == 3 ? 1 : color == 4 ? 2 : color == 6 ? 4 : 0;
  if (!channels) return false;
  std::vector<uint8_t> raw;
  if (!zlibDecompress(idat.data(), idat.size(), raw)) return false;
  size_t stride = static_cast<size_t>(w) * channels;
  if (raw.size() < (stride + 1) * h) return false;
  std::vector<uint8_t> img(stride * h);
  for (int y = 0; y < h; ++y) {
    uint8_t f = raw[y * (stride + 1)];
    const uint8_t* src = &raw[y * (stride + 1) + 1];
    uint8_t* dst = &img[y * stride];
    const uint8_t* up = y > 0 ? dst - stride : nullptr;
    for (size_t i = 0; i < stride; ++i) {
      int a = i >= static_cast<size_t>(channels) ? dst[i - channels] : 0, b = up ? up[i] : 0,
          c = (i >= static_cast<size_t>(channels) && up) ? up[i - channels] : 0;
      int p = 0;
      switch (f) {
        case 0: p = 0; break;
        case 1: p = a; break;
        case 2: p = b; break;
        case 3: p = (a + b) >> 1; break;
        case 4: {
          int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
          p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
          break;
        }
        default: return false;
      }
      dst[i] = static_cast<uint8_t>(src[i] + p);
    }
  }
  rgba.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    uint8_t* o = &rgba[i * 4];
    const uint8_t* s = &img[i * channels];
    switch (color) {
      case 0: o[0] = o[1] = o[2] = s[0], o[3] = 255; break;
      case 2: o[0] = s[0], o[1] = s[1], o[2] = s[2], o[3] = 255; break;
      case 3: {
        size_t k = s[0];
        if (k * 3 + 2 >= palette.size()) return false;
        o[0] = palette[k * 3], o[1] = palette[k * 3 + 1], o[2] = palette[k * 3 + 2];
        o[3] = k < trns.size() ? trns[k] : 255;
        break;
      }
      case 4: o[0] = o[1] = o[2] = s[0], o[3] = s[1]; break;
      default: std::memcpy(o, s, 4);
    }
  }
  return true;
}

bool writePng(const std::string& path, const Image& img, bool encoded) {
  std::vector<uint8_t> bytes(img.px.size() * 4);
  for (size_t i = 0; i < img.px.size(); ++i) {
    const Vec4& p = img.px[i];
    for (int c = 0; c < 3; ++c) {
      float v = encoded ? saturate(p[c]) : linearToSrgb(p[c]);
      bytes[i * 4 + c] = static_cast<uint8_t>(std::lround(v * 255));
    }
    bytes[i * 4 + 3] = static_cast<uint8_t>(std::lround(saturate(p.w) * 255));
  }
  std::vector<uint8_t> png = encodePng(bytes.data(), img.width, img.height);
  return writeFile(path, png.data(), png.size());
}

bool readPng(const std::string& path, Image& img, bool toLinear) {
  std::vector<uint8_t> data;
  if (!readFile(path, data)) return false;
  std::vector<uint8_t> rgba;
  int w, h;
  if (!decodePng(data.data(), data.size(), rgba, w, h)) return false;
  img = Image(w, h);
  for (size_t i = 0; i < img.px.size(); ++i) {
    Vec4& p = img.px[i];
    for (int c = 0; c < 3; ++c) {
      float v = rgba[i * 4 + c] / 255.0f;
      p[c] = toLinear ? srgbToLinear(v) : v;
    }
    p.w = rgba[i * 4 + 3] / 255.0f;
  }
  return true;
}

bool writePfm(const std::string& path, const Image& img) {
  std::string header = "PF\n" + std::to_string(img.width) + " " + std::to_string(img.height) + "\n-1.0\n";
  std::vector<uint8_t> out(header.begin(), header.end());
  for (int y = img.height - 1; y >= 0; --y)
    for (int x = 0; x < img.width; ++x)
      for (int c = 0; c < 3; ++c) {
        float v = img.at(x, y)[c];
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        out.insert(out.end(), b, b + 4);
      }
  return writeFile(path, out.data(), out.size());
}

}  // namespace loop
