// BGRA helpers of the screen recorder: overlay blits, outlines, click halos.
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "pixel_ops.h"

namespace mvp {
namespace {

// Pixels as 32-bit words: BGRA bytes read as 0xAARRGGBB on little-endian.
struct Canvas {
    Canvas(int w, int h, uint32_t fill) : width(w), height(h), pixels(static_cast<size_t>(w) * h, fill) {}

    pixel_ops::BgraImage Image() { return {reinterpret_cast<uint8_t*>(pixels.data()), width, height, width * 4}; }
    uint32_t At(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
    uint32_t Red(int x, int y) const { return (At(x, y) >> 16) & 0xFF; }

    int width;
    int height;
    std::vector<uint32_t> pixels;
};

const uint8_t* Bytes(const std::vector<uint32_t>& pixels) { return reinterpret_cast<const uint8_t*>(pixels.data()); }

// 3x3 source whose pixels are numbered 1..9 row by row.
std::vector<uint32_t> Numbered3x3() { return {1, 2, 3, 4, 5, 6, 7, 8, 9}; }

TEST(PixelOpsBgraTest, BlitIsClippedToTheDestination) {
    const std::vector<uint32_t> src = Numbered3x3();
    Canvas bottom_right(4, 4, 0);
    pixel_ops::BlitBgra(Bytes(src), 12, 3, 3, bottom_right.Image(), 2, 2, false);
    EXPECT_EQ(bottom_right.At(2, 2), 1u);
    EXPECT_EQ(bottom_right.At(3, 2), 2u);
    EXPECT_EQ(bottom_right.At(2, 3), 4u);
    EXPECT_EQ(bottom_right.At(3, 3), 5u);
    EXPECT_EQ(bottom_right.At(1, 1), 0u);

    Canvas top_left(4, 4, 0);
    pixel_ops::BlitBgra(Bytes(src), 12, 3, 3, top_left.Image(), -1, -1, false);
    EXPECT_EQ(top_left.At(0, 0), 5u);
    EXPECT_EQ(top_left.At(1, 1), 9u);
    EXPECT_EQ(top_left.At(2, 2), 0u);
}

TEST(PixelOpsBgraTest, MirroredBlitFlipsEachRow) {
    const std::vector<uint32_t> src = Numbered3x3();
    Canvas whole(3, 3, 0);
    pixel_ops::BlitBgra(Bytes(src), 12, 3, 3, whole.Image(), 0, 0, true);
    EXPECT_EQ(whole.At(0, 0), 3u);
    EXPECT_EQ(whole.At(2, 0), 1u);
    EXPECT_EQ(whole.At(0, 2), 9u);

    // Clipped on the right: the visible columns are the source's last ones.
    Canvas clipped(3, 1, 0);
    pixel_ops::BlitBgra(Bytes(src), 12, 3, 1, clipped.Image(), 1, 0, true);
    EXPECT_EQ(clipped.At(0, 0), 0u);
    EXPECT_EQ(clipped.At(1, 0), 3u);
    EXPECT_EQ(clipped.At(2, 0), 2u);
}

TEST(PixelOpsBgraTest, OutlineDrawsOnlyTheBorder) {
    Canvas canvas(6, 6, 0);
    pixel_ops::OutlineRectBgra(canvas.Image(), 1, 1, 4, 4, 1, 0x112233);
    const uint32_t line = 0xFF112233u;
    for (int i = 1; i <= 4; ++i) {
        EXPECT_EQ(canvas.At(i, 1), line) << "top " << i;
        EXPECT_EQ(canvas.At(i, 4), line) << "bottom " << i;
        EXPECT_EQ(canvas.At(1, i), line) << "left " << i;
        EXPECT_EQ(canvas.At(4, i), line) << "right " << i;
    }
    EXPECT_EQ(canvas.At(2, 2), 0u);
    EXPECT_EQ(canvas.At(3, 3), 0u);
    EXPECT_EQ(canvas.At(0, 0), 0u);
    EXPECT_EQ(canvas.At(5, 5), 0u);
}

TEST(PixelOpsBgraTest, DiscBlendsTowardsItsColorWithASoftEdge) {
    Canvas canvas(21, 21, 0xFF000000u);
    pixel_ops::BlendDiscBgra(canvas.Image(), 10, 10, 5, 0xFF0000, 1.0f);
    EXPECT_EQ(canvas.At(10, 10) & 0xFFFFFFu, 0xFF0000u);
    EXPECT_EQ(canvas.At(0, 0) & 0xFFFFFFu, 0u);
    // On the rim (distance == radius) half of the pixel is covered.
    EXPECT_NEAR(static_cast<double>(canvas.Red(15, 10)), 128.0, 2.0);

    Canvas faint(21, 21, 0xFF000000u);
    pixel_ops::BlendDiscBgra(faint.Image(), 10, 10, 5, 0xFF0000, 0.5f);
    EXPECT_NEAR(static_cast<double>(faint.Red(10, 10)), 128.0, 1.0);

    Canvas corner(8, 8, 0xFF000000u);
    pixel_ops::BlendDiscBgra(corner.Image(), 0, 0, 4, 0x00FF00, 1.0f);
    EXPECT_EQ((corner.At(0, 0) >> 8) & 0xFF, 0xFFu);
}

}  // namespace
}  // namespace mvp
