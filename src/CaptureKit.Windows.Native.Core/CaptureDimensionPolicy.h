#pragma once

#include <cstdint>

struct NormalizedCaptureDimensions
{
    uint32_t width;
    uint32_t height;
};

/// <summary>
/// Normalizes capture dimensions for H.264 encoders while allowing the one-pixel
/// overrun that can be introduced by logical-to-physical DPI rounding.
/// </summary>
inline bool TryNormalizeCaptureDimensions(
    uint32_t requestedWidth,
    uint32_t requestedHeight,
    uint32_t availableWidth,
    uint32_t availableHeight,
    NormalizedCaptureDimensions* result)
{
    if (!result ||
        requestedWidth == 0 || requestedHeight == 0 ||
        availableWidth < 2 || availableHeight < 2)
    {
        return false;
    }

    constexpr uint64_t RoundingTolerance = 1;
    if (static_cast<uint64_t>(requestedWidth) > static_cast<uint64_t>(availableWidth) + RoundingTolerance ||
        static_cast<uint64_t>(requestedHeight) > static_cast<uint64_t>(availableHeight) + RoundingTolerance)
    {
        return false;
    }

    uint32_t width = (requestedWidth < availableWidth ? requestedWidth : availableWidth) & ~uint32_t{1};
    uint32_t height = (requestedHeight < availableHeight ? requestedHeight : availableHeight) & ~uint32_t{1};
    if (width < 2 || height < 2)
    {
        return false;
    }

    *result = { width, height };
    return true;
}
