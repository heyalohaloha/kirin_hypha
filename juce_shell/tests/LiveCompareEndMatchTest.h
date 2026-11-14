#pragma once
#include "../src/PluginEditor.h"

// Exercise the shipping editor's MATCH and AUTO handlers without opening a window.
namespace end_match_test
{
template <class Tag, typename Tag::Type Member> struct Access
{ friend typename Tag::Type member (Tag) { return Member; } };
struct Apply
{
    using Type = void (KirinHyphaEditor::*) (const hypha::live_compare::MatchPlan&, hypha::live_compare::MatchChoice);
    friend Type member (Apply);
};
template struct Access<Apply, &KirinHyphaEditor::applyLiveCompareChoice>;
struct FollowChoice { using Type = void (KirinHyphaEditor::*) (int); friend Type member (FollowChoice); };
template struct Access<FollowChoice, &KirinHyphaEditor::handleLiveCompareFollowChoice>;
struct Follow
{
    using Type = void (KirinHyphaEditor::*) (const hypha::live_compare::Status&, double);
    friend Type member (Follow);
};
template struct Access<Follow, &KirinHyphaEditor::followLiveCompare>;
struct Match { using Type = void (KirinHyphaEditor::*)(); friend Type member (Match); };
template struct Access<Match, &KirinHyphaEditor::requestListenMatch>;
struct AutoAccess {};
bool autoOn (const KirinHyphaEditor&, AutoAccess);
void autoSetup (KirinHyphaEditor&, AutoAccess);
void autoDue (KirinHyphaEditor&, AutoAccess);
double autoPeak (const KirinHyphaEditor&, AutoAccess);
double autoNext (const KirinHyphaEditor&, AutoAccess);
template <auto Member> struct AutoMember
{
    friend bool autoOn (const KirinHyphaEditor& e, AutoAccess) { return (e.*Member).on; }
    friend void autoSetup (KirinHyphaEditor& e, AutoAccess)
    {
        auto& a = e.*Member;
        a.on = true; a.approvedPreDb = 20.0 * std::log10 (2.0); a.ceilingDbtp = -1.0;
        a.peakDbtp = -10.0; a.nextAt = juce::Time::getMillisecondCounterHiRes() * 0.001 + 60.0;
    }
    friend void autoDue (KirinHyphaEditor& e, AutoAccess) { (e.*Member).nextAt = 0.0; }
    friend double autoPeak (const KirinHyphaEditor& e, AutoAccess) { return (e.*Member).peakDbtp; }
    friend double autoNext (const KirinHyphaEditor& e, AutoAccess) { return (e.*Member).nextAt; }
};
template struct AutoMember<&KirinHyphaEditor::liveCompareAuto>;

inline void apply (KirinHyphaEditor& e, const hypha::live_compare::MatchPlan& plan)
{ (e.*member (Apply {})) (plan, hypha::live_compare::MatchChoice::basis); }
inline void enableAuto (KirinHyphaEditor& e) { (e.*member (FollowChoice {})) (3); }
inline void toggle (KirinHyphaEditor& e) { (e.*member (Match {})) (); }
inline void follow (KirinHyphaEditor& e, const hypha::live_compare::Status& status)
{ (e.*member (Follow {})) (status, juce::Time::getMillisecondCounterHiRes() * 0.001); }
}
