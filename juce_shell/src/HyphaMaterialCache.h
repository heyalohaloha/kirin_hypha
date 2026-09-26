#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

// Static surface material (panels, observation wells, control plates) depends only on its size, a
// few parameters and the device scale, yet its gradients and clipped fills cost milliseconds per
// paint at 200-300% on high-density displays, and the pages that show it repaint 10-30 times a
// second. A material requested again at the same key is drawn once into a device-resolution image
// and only blitted from then on. A key seen once (a size passing by during a corner drag) is painted
// directly, so a resize never pays for an image it will not reuse. The images exist only while an
// editor holds a Lifetime; without one every material is painted directly.
namespace hypha::material_cache
{
struct Key
{
    int kind = 0;
    std::array<float, 4> parameters {};
    float width = 0.0f;
    float height = 0.0f;
    float scale = 0.0f;

    bool operator== (const Key& other) const noexcept
    {
        const std::equal_to<float> same;
        return kind == other.kind && parameters == other.parameters && same (width, other.width)
            && same (height, other.height) && same (scale, other.scale);
    }
};

// Material drawn outside its own area: a cast shadow above it, a contact shadow below it.
struct Bleed
{
    float top = 0.0f;
    float bottom = 0.0f;
};

class Store
{
public:
    static constexpr size_t budgetBytes = 24u * 1024u * 1024u;
    static constexpr size_t maximumEntries = 192u;

    struct Lookup
    {
        juce::Image image;
        bool build = false;
    };

    Lookup lookup (const Key& key)
    {
        const std::scoped_lock lock (mutex);
        const auto now = ++clock;
        for (auto& entry : entries)
            if (entry.key == key)
            {
                entry.lastUse = now;
                return { entry.image, ! entry.image.isValid() };
            }
        if (entries.size() >= maximumEntries)
            evictOldest (false);
        entries.push_back ({ key, {}, now });
        return {};
    }

    void keep (const Key& key, const juce::Image& image)
    {
        const std::scoped_lock lock (mutex);
        const auto bytes = bytesOf (image);
        if (bytes > budgetBytes)
            return;
        for (auto& entry : entries)
            if (entry.key == key)
            {
                totalBytes -= bytesOf (entry.image);
                entry.image = image;
                entry.lastUse = ++clock;
                totalBytes += bytes;
                break;
            }
        while (totalBytes > budgetBytes && evictOldest (true)) {}
    }

    size_t bytes() const
    {
        const std::scoped_lock lock (mutex);
        return totalBytes;
    }

    // A software raster of this size for a painter that draws into it and blits it within one
    // paint; the three most recent sizes are kept so steady pages never reallocate.
    juce::Image scratch (int width, int height)
    {
        const std::scoped_lock lock (mutex);
        for (auto image = scratches.begin(); image != scratches.end(); ++image)
            if (image->getWidth() == width && image->getHeight() == height)
            {
                const auto found = *image;
                scratches.erase (image);
                scratches.insert (scratches.begin(), found);
                return found;
            }
        // Native pixels: CoreGraphics draws them without first copying the whole raster.
        juce::Image image (juce::Image::ARGB, width, height, true, juce::NativeImageType {});
        scratches.insert (scratches.begin(), image);
        if (scratches.size() > 3u)
            scratches.pop_back();
        return image;
    }

private:
    struct Entry
    {
        Key key;
        juce::Image image;
        uint64_t lastUse = 0u;
    };

    static size_t bytesOf (const juce::Image& image) noexcept
    {
        return image.isValid() ? (size_t) image.getWidth() * (size_t) image.getHeight() * 4u : 0u;
    }

    // Drops the least recently used entry (only one holding an image, when asked), never the one
    // used last.
    bool evictOldest (bool withImage)
    {
        auto oldest = entries.end();
        for (auto entry = entries.begin(); entry != entries.end(); ++entry)
            if ((! withImage || entry->image.isValid()) && entry->lastUse != clock
                && (oldest == entries.end() || entry->lastUse < oldest->lastUse))
                oldest = entry;
        if (oldest == entries.end())
            return false;
        totalBytes -= bytesOf (oldest->image);
        entries.erase (oldest);
        return true;
    }

    mutable std::mutex mutex;
    std::vector<Entry> entries;
    std::vector<juce::Image> scratches;
    size_t totalBytes = 0u;
    uint64_t clock = 0u;
};

// Held by each editor: the images are released when the last editor closes.
struct Lifetime
{
    juce::SharedResourcePointer<Store> store;
};

// A cleared raster of this size: reused while an editor is open, otherwise new. Draw into it with
// the software renderer (`juce::LowLevelGraphicsSoftwareRenderer`) or through its pixels.
inline juce::Image scratchImage (int width, int height)
{
    if (const auto store = juce::SharedResourcePointer<Store>::getSharedObjectWithoutCreating())
    {
        auto image = (*store)->scratch (width, height);
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < pixels.height; ++y)
            std::memset (pixels.getLinePointer (y), 0, (size_t) (pixels.width * pixels.pixelStride));
        return image;
    }
    return juce::Image (juce::Image::ARGB, width, height, true, juce::NativeImageType {});
}

// A static image `build` draws at this exact pixel size, for a painter to use as a fill. Invalid
// while the key has been seen only once (or without an editor), so the caller paints directly.
template <typename Build>
juce::Image image (Key key, int pixelWidth, int pixelHeight, Build&& build)
{
    const auto store = juce::SharedResourcePointer<Store>::getSharedObjectWithoutCreating();
    if (! store.has_value() || pixelWidth <= 0 || pixelHeight <= 0
        || pixelWidth > 4'096 || pixelHeight > 4'096)
        return {};
    auto found = (*store)->lookup (key);
    if (found.image.isValid() || ! found.build)
        return found.image;
    juce::Image built (juce::Image::ARGB, pixelWidth, pixelHeight, true, juce::SoftwareImageType {});
    {
        juce::Graphics pixels (built);
        build (pixels);
    }
    (*store)->keep (key, built);
    return built;
}

// Draws `paint (graphics, area)` through the cache. `paint` must depend only on the key and the
// area's size, never on its position.
template <typename Paint>
void draw (juce::Graphics& g, juce::Rectangle<float> area, Key key, Bleed bleed, Paint&& paint)
{
    const auto store = juce::SharedResourcePointer<Store>::getSharedObjectWithoutCreating();
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto extent = area.withTop (area.getY() - bleed.top)
                            .withBottom (area.getBottom() + bleed.bottom);
    if (! store.has_value() || area.isEmpty() || ! std::isfinite (scale) || scale <= 0.0f
        || scale > 4.0f || extent.getWidth() * scale > 4'096.0f || extent.getHeight() * scale > 4'096.0f)
    {
        paint (g, area);
        return;
    }
    key.width = area.getWidth();
    key.height = area.getHeight();
    key.scale = scale;
    auto found = (*store)->lookup (key);
    if (! found.image.isValid() && ! found.build)
    {
        paint (g, area);
        return;
    }
    const auto pixelWidth = (int) std::ceil (extent.getWidth() * scale);
    const auto pixelHeight = (int) std::ceil (extent.getHeight() * scale);
    if (! found.image.isValid())
    {
        juce::Image image (juce::Image::ARGB, pixelWidth, pixelHeight, true);
        {
            juce::Graphics pixels (image);
            pixels.addTransform (juce::AffineTransform::translation (0.0f, bleed.top).scaled (scale));
            paint (pixels, area.withPosition (0.0f, 0.0f));
        }
        (*store)->keep (key, image);
        found.image = image;
    }
    const juce::Graphics::ScopedSaveState saved (g);
    g.setOpacity (1.0f);
    // Already device resolution: one image pixel per device pixel, never smoothed.
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (found.image, { extent.getX(), extent.getY(), (float) pixelWidth / scale,
                                (float) pixelHeight / scale });
}
}
