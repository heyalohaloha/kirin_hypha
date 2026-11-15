#pragma once

#include "HyphaTextStyle.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace hypha::reference_ui::axis_labels
{
struct Mark
{
    float x = 0.0f;
    juce::String text;
    juce::Rectangle<float> bounds;
    bool visible = false;
};

// The endpoints and zero retain their labels. A zero close to an endpoint may move its label
// along the axis, while its grid line remains at the exact value.
template<class TextFor>
std::vector<Mark> layout (double minimum, double maximum, double step, float left, float right,
                          juce::Rectangle<float> axis, const juce::Font& font, TextFor textFor)
{
    std::vector<Mark> marks;
    int zero = -1;
    for (auto value = minimum; value <= maximum + 1.0e-9; value += step)
    {
        const auto x = left + static_cast<float> ((value - minimum) / (maximum - minimum)) * (right - left);
        auto text = textFor (value);
        const auto width = std::ceil (text_style::shownWidth (font, text)) + 2.0f;
        if (std::abs (value) < 1.0e-9) zero = static_cast<int> (marks.size());
        marks.push_back ({ x, std::move (text), { x - width * 0.5f, axis.getY(), width, axis.getHeight() }, false });
    }
    if (marks.empty()) return marks;
    marks.front().visible = marks.back().visible = true;
    if (zero > 0 && zero + 1 < static_cast<int> (marks.size()))
    {
        auto& label = marks[static_cast<size_t> (zero)];
        auto lower = marks.front().bounds.getRight() + 2.0f;
        auto upper = marks.back().bounds.getX() - label.bounds.getWidth() - 2.0f;
        if (lower > upper)
        {
            const auto shift = (lower - upper) * 0.5f + 1.0f;
            marks.front().bounds.translate (-shift, 0.0f);
            marks.back().bounds.translate (shift, 0.0f);
            lower -= shift; upper += shift;
        }
        label.bounds.setX (std::clamp (label.bounds.getX(), lower, upper));
        label.visible = true;
    }
    for (auto& label : marks)
    {
        if (label.visible) continue;
        const auto padded = label.bounds.expanded (1.0f, 0.0f);
        const bool overlap = std::any_of (marks.begin(), marks.end(), [&] (const Mark& other)
        { return other.visible && padded.intersects (other.bounds); });
        label.visible = ! overlap;
    }
    return marks;
}
}
