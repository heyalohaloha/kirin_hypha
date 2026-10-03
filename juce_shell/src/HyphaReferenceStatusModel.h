#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaReferenceGuide.h"

// H9: 状態の帯（方向設計 §3.3）。今の役の状態を 3 つに分ける：聴ける（水色）／準備中（金、何を待って
// いるか・進めるために何をするか）／できない（灰、理由と直し方）。無言にしない（R-28）。色で採点しない。
namespace hypha::reference_ui
{
struct State;

enum class StatusKind { ready, waiting, unable };

struct StatusLine
{
    StatusKind kind = StatusKind::waiting;
    juce::String text;
};

// 段階が「聴ける」「準備中（待てば進む・DAW の再生で進む）」「できない（利用者か Kirin OS の操作が要る）」の
// どれか。全ての段階がどれか 1 つに入る（テストで固定する）。
StatusKind kindOf (SourceStep) noexcept;
StatusLine referenceStatusLine (const State&);
juce::Colour statusColour (StatusKind) noexcept;
void paintStatusDot (juce::Graphics&, juce::Rectangle<int> line, StatusKind);
}
