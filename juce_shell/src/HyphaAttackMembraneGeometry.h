#pragma once
#include "HyphaAttackSpecimenPainter.h"

namespace hypha::attack_membrane
{
constexpr int segments = 44;
struct Point { float x, y, half, twist, shear, ridge, u; };
using Surface = std::array<Point, segments + 1>;
struct Sheet
{
    std::array<juce::Path, 5> fills;
    juce::Point<float> lightStart, lightEnd;
};
struct Geometry
{
    std::array<Sheet, 4> sheets;
    juce::Path front;
    float coreThickness = 0, opening = 0, wrinkle = 0, frontX = 0;
};
inline juce::Point<float> position (const Point& p, float v)
{
    return { p.x + p.shear * v, p.y + p.half * ((v*.75f + v*v*.22f - .10f) * p.twist
        + .48f * std::sin (v*2.25f + p.u*3)) + p.ridge * (1-v*v) };
}
inline void boundary (juce::Path& path, const Surface& points, float v, bool reverse, int first = 0)
{
    std::array<juce::Point<float>, segments + 1> curve;
    const auto n = segments - first;
    for (int i = 0; i <= n; ++i)
        curve[static_cast<std::size_t> (i)] = position (
            points[static_cast<std::size_t> (reverse ? segments-i : i+first)], v);
    if (reverse) path.lineTo (curve[0]); else path.startNewSubPath (curve[0]);
    for (int i = 0; i < n; ++i)
    {
        const auto a = curve[static_cast<std::size_t> (std::max (0,i-1))];
        const auto b = curve[static_cast<std::size_t> (i)];
        const auto c = curve[static_cast<std::size_t> (i+1)];
        const auto d = curve[static_cast<std::size_t> (std::min (n,i+2))];
        path.cubicTo (b+(c-a)/6.0f, c-(d-b)/6.0f, c);
    }
}
inline juce::Path strip (const Surface& points, float lo, float hi, int first = 0)
{
    juce::Path path;
    path.preallocateSpace ((segments-first+1)*14+8);
    boundary (path, points, lo, false, first); boundary (path, points, hi, true, first);
    path.closeSubPath(); return path;
}
inline float gaussian (float u, float centre, float spread)
{ const auto v = (u-centre)/spread; return std::exp (-v*v); }
inline Geometry geometry (juce::Rectangle<float> area, attack_specimen::FeatureAmounts raw,
                          const attack_motion::Motion& motion = {})
{
    Geometry result;
    if (! std::isfinite (area.getX()) || ! std::isfinite (area.getY())
        || ! std::isfinite (area.getWidth()) || ! std::isfinite (area.getHeight())
        || area.getWidth() < 4 || area.getHeight() < 4) return result;
    using attack_motion::unit;
    const auto s=unit (raw.strength), b=unit (raw.brightness), x=unit (raw.texture), t=unit (raw.transient);
    struct Profile { float start, end, lift, phase, span, bias; };
    constexpr std::array<Profile, 4> profiles {{
        {.075f,.82f,-.115f,.1f,.150f,-1}, {.135f,.87f,.058f,1.3f,.115f,.18f},
        {.115f,.82f,.115f,2.6f,.123f,1}, {.250f,.85f,-.035f,3.4f,.073f,-.18f} }};
    result.coreThickness=area.getHeight()*s*.12f; result.opening=area.getHeight()*b*.108f;
    result.wrinkle=area.getHeight()*x*.018f; result.frontX=area.getX()+area.getWidth()*(.87f+t*.078f);
    for (std::size_t layer=0; layer<profiles.size(); ++layer)
    {
        const auto& p=profiles[layer]; Surface points {};
        for (int j=0; j<=segments; ++j)
        {
            const auto u=static_cast<float> (j)/segments;
            const auto envelope=std::pow (std::max (0.0f,std::sin (juce::MathConstants<float>::pi*u)),.82f);
            const auto fold=gaussian (u,.57f,.21f);
            const auto wave=std::sin (juce::MathConstants<float>::pi*std::min (1.0f,u/.76f));
            const auto tap=std::min (5,static_cast<int> (u*6));
            const auto bend=[&] (int at) { const auto v=motion.bend[static_cast<std::size_t> (at)];
                return std::isfinite (v)?juce::jlimit (-.24f,.24f,v):0.0f; };
            const auto curvature=bend (tap)+(bend (tap+1)-bend (tap))*(u*6-static_cast<float> (tap));
            const auto tissue=(p.span*.37f+s*.12f*gaussian (u,.61f,.24f))*(layer%2==0?.78f:1.0f);
            points[static_cast<std::size_t> (j)]={
                area.getX()+area.getWidth()*(p.start+(p.end-p.start)*u+t*.078f*u*u*u),
                area.getY()+area.getHeight()*(.51f+p.lift*envelope+.049f*std::sin (u*6+p.phase)*envelope
                    +p.bias*b*.108f*envelope+x*.018f*fold*std::sin (u*24+p.phase)
                    +curvature*.07f*wave*wave*(layer%2==0?-1.0f:1.0f)),
                area.getHeight()*tissue*envelope,std::cos (u*3.3f+p.phase*.55f),
                area.getWidth()*.022f*envelope*std::sin (u*4+p.phase),
                area.getHeight()*x*.010f*fold*std::sin (u*35+p.phase),u};
        }
        auto& sheet=result.sheets[layer];
        sheet.fills={strip (points,-1,1),strip (points,-.82f,-.05f),strip (points,-.04f,.63f),
                     strip (points,.57f,.586f),strip (points,-.80f,-.784f)};
        const auto a=position (points[27],-1), z=position (points[27],1);
        sheet.lightStart={area.getX()+area.getWidth()*.38f,std::min (a.y,z.y)-area.getHeight()*.0175f};
        sheet.lightEnd={area.getX()+area.getWidth()*.70f,std::max (a.y,z.y)+area.getHeight()*.028f};
        if (layer==1) result.front=strip (points,-1,1,segments*3/4);
    }
    return result;
}
}
