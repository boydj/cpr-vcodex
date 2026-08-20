#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>

class GfxRenderer;

struct ImageDimensions {
  int16_t width;
  int16_t height;
};

struct RenderConfig {
  int x, y;
  int maxWidth, maxHeight;
  bool useGrayscale = true;
  bool useDithering = true;
  bool performanceMode = false;
  bool useExactDimensions = false;  // If true, use maxWidth/maxHeight as exact output size (no recalculation)
  std::string cachePath;            // If non-empty, decoder will write pixel cache to this path
};

class ImageToFramebufferDecoder {
 public:
  virtual ~ImageToFramebufferDecoder() = default;

  virtual bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) = 0;

  virtual bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const = 0;

  virtual const char* getFormatName() const = 0;

  // Shared by header probes and decoders so dimensions are validated before
  // narrowing to the int16_t layout representation.
  static bool validateAndStoreDimensions(int64_t width, int64_t height, ImageDimensions& out, const char* format);

  // Long image decodes must periodically yield to keep the idle task and its
  // watchdog serviced. The caller owns the timestamp so callbacks stay reentrant.
  static void yieldDuringDecode(uint32_t& lastYieldMs);

 protected:
  static constexpr int64_t MAX_SOURCE_DIMENSION = INT16_MAX;
  // Keep the existing X4 decode-time cap; upstream's 8 MP expansion needs
  // separate device measurements before it is safe for vCodex.
  static constexpr int64_t MAX_SOURCE_PIXELS = 3145728;  // 2048 * 1536

  void warnUnsupportedFeature(const std::string& feature, const std::string& imagePath);
};
