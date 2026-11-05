#pragma once
// JPEG pictures, read by our own decoder (no outside libraries): baseline
// (sequential Huffman) JPEGs with any chroma subsampling, grey or YCbCr,
// with restart markers. That is what Blender and most cameras write.
// Progressive and arithmetic-coded JPEGs are refused with a plain reason.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace loop {

// Decodes to 8-bit RGBA (alpha 255), top row first. False with a reason.
bool decodeJpeg(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, int& width, int& height, std::string* error = nullptr);

}  // namespace loop
