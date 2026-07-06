/* Stub: no audio debugging on S3 */
#pragma once
#include <vector>
#include <cstdint>
class AudioDebugger { public: virtual ~AudioDebugger() = default; void Feed(const std::vector<int16_t>&) {} };
