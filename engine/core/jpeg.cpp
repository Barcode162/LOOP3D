#include "engine/core/jpeg.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace loop {
namespace {

constexpr int kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                             41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                             30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// A Huffman table in the spec's canonical form (ITU T.81, F.2.2.3).
struct Huffman {
  bool present = false;
  int maxcode[18];   // largest code of each length, -1 when none
  int valptr[17];    // index of the first value of each length
  int mincode[17];
  std::vector<uint8_t> values;
};

struct Component {
  int id = 0, h = 1, v = 1, quant = 0;
  int dcTable = 0, acTable = 0;
  int blocksW = 0, blocksH = 0;  // blocks across and down in this plane (padded to whole MCUs)
  std::vector<uint8_t> plane;    // blocksW*8 by blocksH*8 samples
  int pred = 0;
};

struct Decoder {
  const uint8_t* d;
  size_t n, pos = 0;
  std::string err;
  uint16_t quant[4][64] = {};
  Huffman dc[4], ac[4];
  std::vector<Component> comps;
  int width = 0, height = 0, hmax = 1, vmax = 1, mcusX = 0, mcusY = 0;
  int restartInterval = 0;
  // bit reader
  uint32_t bits = 0;
  int bitCount = 0;
  bool hitMarker = false;
  // the 8x8 inverse DCT's cosine table: c[x][u] = C(u)/2 cos((2x+1)u pi/16)
  float idctTable[8][8];

  bool fail(const std::string& why) {
    if (err.empty()) err = why;
    return false;
  }
  int u8() { return pos < n ? d[pos++] : 0; }
  int u16() { int a = u8(); return (a << 8) | u8(); }

  void fillBits() {
    while (bitCount <= 24) {
      int byte = 0;
      if (!hitMarker && pos < n) {
        byte = d[pos];
        if (byte == 0xFF) {
          int next = pos + 1 < n ? d[pos + 1] : 0;
          if (next == 0x00) {
            pos += 2;
          } else {
            hitMarker = true;  // a marker: feed zeros until the scan ends
            byte = 0;
          }
        } else {
          ++pos;
        }
      }
      bits |= static_cast<uint32_t>(byte) << (24 - bitCount);
      bitCount += 8;
    }
  }
  int getBit() {
    if (bitCount == 0) fillBits();
    int b = static_cast<int>(bits >> 31);
    bits <<= 1, --bitCount;
    return b;
  }
  int receive(int s) {
    int v = 0;
    for (int i = 0; i < s; ++i) v = (v << 1) | getBit();
    return v;
  }
  static int extend(int v, int s) { return s == 0 ? 0 : (v < (1 << (s - 1)) ? v - (1 << s) + 1 : v); }
  int decode(const Huffman& t) {
    int code = 0;
    for (int len = 1; len <= 16; ++len) {
      code = (code << 1) | getBit();
      if (t.maxcode[len] >= 0 && code <= t.maxcode[len]) {
        int idx = t.valptr[len] + code - t.mincode[len];
        return idx >= 0 && idx < static_cast<int>(t.values.size()) ? t.values[idx] : 0;
      }
    }
    err = err.empty() ? "a damaged JPEG (a code that isn't in its tables)" : err;
    return 0;
  }
  void resetBits() { bits = 0, bitCount = 0, hitMarker = false; }

  bool readDqt(int len) {
    size_t end = pos + len - 2;
    while (pos < end) {
      int pq = u8(), id = pq & 15, prec = pq >> 4;
      if (id > 3) return fail("a JPEG quantisation table with a bad number");
      for (int i = 0; i < 64; ++i) quant[id][i] = static_cast<uint16_t>(prec ? u16() : u8());
    }
    pos = end;
    return true;
  }
  bool readDht(int len) {
    size_t end = pos + len - 2;
    while (pos < end) {
      int tc = u8(), cls = tc >> 4, id = tc & 15;
      if (cls > 1 || id > 3) return fail("a JPEG Huffman table with a bad number");
      Huffman& t = cls ? ac[id] : dc[id];
      int counts[17] = {0};
      int total = 0;
      for (int i = 1; i <= 16; ++i) counts[i] = u8(), total += counts[i];
      t.values.resize(total);
      for (int i = 0; i < total; ++i) t.values[i] = static_cast<uint8_t>(u8());
      int code = 0, k = 0;
      for (int len2 = 1; len2 <= 16; ++len2) {
        t.valptr[len2] = k, t.mincode[len2] = code;
        code += counts[len2], k += counts[len2];
        t.maxcode[len2] = counts[len2] ? code - 1 : -1;
        code <<= 1;
      }
      t.maxcode[17] = 0x7fffffff;
      t.present = true;
    }
    pos = end;
    return true;
  }
  bool readSof(int len) {
    size_t end = pos + len - 2;
    if (u8() != 8) return fail("only 8-bit JPEGs are read");
    height = u16(), width = u16();
    int nc = u8();
    if (width <= 0 || height <= 0) return fail("a JPEG with no size");
    if (nc != 1 && nc != 3) return fail("only grey and colour (3-part) JPEGs are read");
    comps.resize(nc);
    for (Component& c : comps) {
      c.id = u8();
      int hv = u8();
      c.h = std::max(1, hv >> 4), c.v = std::max(1, hv & 15);
      c.quant = u8() & 3;
      hmax = std::max(hmax, c.h), vmax = std::max(vmax, c.v);
    }
    pos = end;
    mcusX = (width + 8 * hmax - 1) / (8 * hmax);
    mcusY = (height + 8 * vmax - 1) / (8 * vmax);
    for (Component& c : comps) {
      c.blocksW = mcusX * c.h, c.blocksH = mcusY * c.v;
      c.plane.assign(static_cast<size_t>(c.blocksW) * 8 * c.blocksH * 8, 0);
    }
    return true;
  }

  void idctBlock(const int coef[64], uint8_t* out, int stride) {
    float tmp[64];
    for (int y = 0; y < 8; ++y)      // rows: frequency v → space y
      for (int u = 0; u < 8; ++u) {
        float s = 0;
        for (int v = 0; v < 8; ++v) s += idctTable[y][v] * static_cast<float>(coef[v * 8 + u]);
        tmp[y * 8 + u] = s;
      }
    for (int y = 0; y < 8; ++y)      // columns: frequency u → space x
      for (int x = 0; x < 8; ++x) {
        float s = 0;
        for (int u = 0; u < 8; ++u) s += idctTable[x][u] * tmp[y * 8 + u];
        int val = static_cast<int>(std::lround(s + 128.0f));
        out[y * stride + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
      }
  }

  void decodeBlock(Component& c, int bx, int by) {
    int coef[64] = {0};
    const uint16_t* q = quant[c.quant];
    int s = decode(dc[c.dcTable]);
    c.pred += extend(receive(s), s);
    coef[0] = c.pred * q[0];
    for (int k = 1; k < 64;) {
      int rs = decode(ac[c.acTable]);
      int r = rs >> 4, sz = rs & 15;
      if (sz == 0) {
        if (r != 15) break;  // end of block
        k += 16;
        continue;
      }
      k += r;
      if (k > 63) break;
      coef[kZigzag[k]] = extend(receive(sz), sz) * q[k];
      ++k;
    }
    int stride = c.blocksW * 8;
    idctBlock(coef, &c.plane[static_cast<size_t>(by) * 8 * stride + static_cast<size_t>(bx) * 8], stride);
  }

  // Skips to the next marker after a restart interval (RSTn) and resets.
  void restart() {
    resetBits();
    while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7)) ++pos;
    if (pos + 1 < n) pos += 2;
    for (Component& c : comps) c.pred = 0;
  }

  bool readSos(int len) {
    size_t end = pos + len - 2;
    int ns = u8();
    std::vector<Component*> scan;
    for (int i = 0; i < ns; ++i) {
      int id = u8(), tables = u8();
      Component* found = nullptr;
      for (Component& c : comps)
        if (c.id == id) found = &c;
      if (!found) return fail("a JPEG scan names a part it doesn't have");
      found->dcTable = (tables >> 4) & 3, found->acTable = tables & 3;
      if (!dc[found->dcTable].present || !ac[found->acTable].present) return fail("a JPEG scan uses a missing Huffman table");
      scan.push_back(found);
    }
    pos = end;  // spectral selection and approximation: always 0, 63, 0 in baseline
    resetBits();
    for (Component& c : comps) c.pred = 0;
    int mcu = 0;
    if (scan.size() == 1) {
      // one part on its own: its blocks in plain order, only those covering the picture
      Component& c = *scan[0];
      int bw = (width * c.h / hmax + 7) / 8, bh = (height * c.v / vmax + 7) / 8;
      for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
          if (restartInterval && mcu && mcu % restartInterval == 0) restart();
          decodeBlock(c, bx, by);
          ++mcu;
        }
    } else {
      for (int my = 0; my < mcusY; ++my)
        for (int mx = 0; mx < mcusX; ++mx) {
          if (restartInterval && mcu && mcu % restartInterval == 0) restart();
          for (Component* c : scan)
            for (int v = 0; v < c->v; ++v)
              for (int h = 0; h < c->h; ++h) decodeBlock(*c, mx * c->h + h, my * c->v + v);
          ++mcu;
        }
    }
    if (!err.empty()) return false;
    // find the marker after the scan's data
    while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] != 0x00 && !(d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7))) ++pos;
    resetBits();
    return true;
  }

  bool run() {
    for (int x = 0; x < 8; ++x)
      for (int u = 0; u < 8; ++u) idctTable[x][u] = (u == 0 ? 0.5f / std::sqrt(2.0f) : 0.5f) * std::cos((2 * x + 1) * u * 3.14159265358979f / 16);
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return fail("not a JPEG (no start marker)");
    pos = 2;
    bool frame = false, scanned = false;
    while (pos + 4 <= n) {
      if (d[pos] != 0xFF) { ++pos; continue; }
      int m = d[pos + 1];
      pos += 2;
      if (m == 0xFF) { --pos; continue; }  // fill bytes
      if (m == 0xD9) break;                // end of picture
      if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
      int len = u16();
      if (len < 2 || pos + len - 2 > n) return fail("a damaged JPEG (a section runs past the end)");
      size_t next = pos + len - 2;
      switch (m) {
        case 0xC0: case 0xC1:
          if (!readSof(len)) return false;
          frame = true;
          break;
        case 0xC2: return fail("progressive JPEGs aren't read yet (save it as baseline)");
        case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
          return fail("this kind of JPEG (lossless, hierarchical or arithmetic) isn't read");
        case 0xC4: if (!readDht(len)) return false; break;
        case 0xDB: if (!readDqt(len)) return false; break;
        case 0xDD: restartInterval = u16(); pos = next; break;
        case 0xDA:
          if (!frame) return fail("a JPEG scan before its frame");
          if (!readSos(len)) return false;
          scanned = true;
          continue;  // readSos left pos at the next marker
        default: pos = next; break;
      }
    }
    if (!scanned) return fail("a JPEG with no picture data");
    return err.empty();
  }
};

}  // namespace

bool decodeJpeg(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& width, int& height, std::string* error) {
  Decoder dec;
  dec.d = data, dec.n = size;
  if (!dec.run()) {
    if (error) *error = dec.err;
    return false;
  }
  width = dec.width, height = dec.height;
  rgba.assign(static_cast<size_t>(width) * height * 4, 255);
  auto sample = [&](const Component& c, int x, int y) {
    int pw = c.blocksW * 8, ph = c.blocksH * 8;
    if (c.h == dec.hmax && c.v == dec.vmax) return static_cast<float>(c.plane[static_cast<size_t>(y) * pw + x]);
    // a smaller part (colour at half size): blend the four nearest samples,
    // centred like the encoder's, so colour edges stay smooth
    int partW = std::max(1, (width * c.h + dec.hmax - 1) / dec.hmax), partH = std::max(1, (height * c.v + dec.vmax - 1) / dec.vmax);
    float fx = (x + 0.5f) * c.h / dec.hmax - 0.5f, fy = (y + 0.5f) * c.v / dec.vmax - 0.5f;
    int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    float tx = fx - x0, ty = fy - y0;
    auto at = [&](int sx, int sy) {
      sx = std::clamp(sx, 0, std::min(partW, pw) - 1), sy = std::clamp(sy, 0, std::min(partH, ph) - 1);
      return static_cast<float>(c.plane[static_cast<size_t>(sy) * pw + sx]);
    };
    float a = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx, b = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx;
    return a * (1 - ty) + b * ty;
  };
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
      if (dec.comps.size() == 1) {
        p[0] = p[1] = p[2] = static_cast<uint8_t>(sample(dec.comps[0], x, y));
        continue;
      }
      float Y = sample(dec.comps[0], x, y), cb = sample(dec.comps[1], x, y) - 128, cr = sample(dec.comps[2], x, y) - 128;
      auto to8 = [](float v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255)); };
      p[0] = to8(Y + 1.402f * cr);
      p[1] = to8(Y - 0.344136f * cb - 0.714136f * cr);
      p[2] = to8(Y + 1.772f * cb);
    }
  return true;
}

}  // namespace loop
