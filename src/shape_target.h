#pragma once
#include <vector>
#include <utility>
#include <cmath>
#include <random>
#include <algorithm>
#include <string>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// ── Shape catalogue ────────────────────────────────────────────────────────────
enum class ShapeType : int {
    Circle=0, Ring, Triangle, Square, Diamond,
    Pentagon, Hexagon, Star5, Star6, Cross, Arrow, Heart,
    COUNT
};

static const char* kShapeNames[] = {
    "Circle","Ring","Triangle","Square","Diamond",
    "Pentagon","Hexagon","Star (5pt)","Star (6pt)",
    "Cross","Arrow","Heart"
};

// ─────────────────────────────────────────────────────────────────────────────
namespace shape_impl {

using Poly = std::vector<std::pair<float,float>>;

// Ray-casting even-odd point-in-polygon
static bool pip(float px, float py, const Poly& v) {
    int n=(int)v.size(); bool in=false;
    for (int i=0,j=n-1; i<n; j=i++) {
        float xi=v[i].first, yi=v[i].second;
        float xj=v[j].first, yj=v[j].second;
        if (((yi>py)!=(yj>py)) && (px < (xj-xi)*(py-yi)/(yj-yi)+xi))
            in=!in;
    }
    return in;
}

// Regular N-gon (rotated so vertex 0 is at the top)
static Poly polygon(int n, float r, float rot=0.f) {
    Poly v(n);
    for (int i=0;i<n;i++) {
        float a=(float)i*2.f*(float)M_PI/n + rot;
        v[i]={cosf(a)*r, sinf(a)*r};
    }
    return v;
}

// N-pointed star: alternating outer / inner vertices
static Poly star(int n, float outer, float inner, float rot=0.f) {
    Poly v(n*2);
    for (int i=0;i<n*2;i++) {
        float a=(float)i*(float)M_PI/n + rot;
        float r=(i%2==0) ? outer : inner;
        v[i]={cosf(a)*r, sinf(a)*r};
    }
    return v;
}

struct ShapeData { ShapeType t; Poly poly; };

// Pre-compute polygon data once per shape call (cheap, avoids per-sample alloc)
static ShapeData prepare(ShapeType t) {
    const float up=-(float)M_PI*0.5f;   // rotate so apex faces screen-top
    ShapeData d; d.t=t;
    switch(t) {
    case ShapeType::Triangle: d.poly=polygon(3,1.f,up);         break;
    case ShapeType::Pentagon:  d.poly=polygon(5,1.f,up);         break;
    case ShapeType::Hexagon:   d.poly=polygon(6,1.f,up);         break;
    case ShapeType::Star5:     d.poly=star(5,1.f,0.38f,up);      break;
    case ShapeType::Star6:     d.poly=star(6,1.f,0.50f,up);      break;
    case ShapeType::Cross: {
        // 12-vertex cross / plus sign
        const float w=0.28f;
        d.poly={
            {-w,1.f},{w,1.f},{w,w},{1.f,w},{1.f,-w},{w,-w},
            {w,-1.f},{-w,-1.f},{-w,-w},{-1.f,-w},{-1.f,w},{-w,w}
        };
        break;
    }
    case ShapeType::Arrow: {
        // Arrow pointing right (+x); user rotates via rotation param
        // Single non-convex polygon: arrowhead + shaft
        const float hw=0.22f;   // shaft half-height
        d.poly={
            {1.f,0.f},
            {-0.1f, 0.65f},{-0.1f, hw},
            {-1.f,  hw},   {-1.f, -hw},
            {-0.1f,-hw},   {-0.1f,-0.65f}
        };
        break;
    }
    default: break;     // Circle, Ring, Square, Diamond, Heart use analytic tests
    }
    return d;
}

static bool test(const ShapeData& d, float px, float py) {
    switch(d.t) {
    case ShapeType::Circle:  return px*px+py*py <= 1.f;
    case ShapeType::Ring: {
        float r=sqrtf(px*px+py*py); return r>=0.5f && r<=1.f;
    }
    case ShapeType::Square:  return fabsf(px)<=0.9f && fabsf(py)<=0.9f;
    case ShapeType::Diamond: return fabsf(px)+fabsf(py)<=1.f;
    case ShapeType::Heart: {
        // (x²+y²−1)³ ≤ x²y³  — negate py so bumps face screen-top
        float hx=px*1.1f, hy=(-py)*1.1f;
        float t=hx*hx+hy*hy-1.f;
        return t*t*t <= hx*hx*hy*hy*hy;
    }
    default: return !d.poly.empty() && pip(px,py,d.poly);
    }
}

} // namespace shape_impl

// ── Public API ─────────────────────────────────────────────────────────────────
// Returns up to min(count, 300'000) interleaved [x,y,...] target positions
// for the given shape, centred at (cx,cy), sized by radius (px), rotated by
// rotRadians. The caller should set targetCount = positions.size()/2.
inline std::vector<float> generateShapeTargets(
    ShapeType type,
    float cx, float cy,
    float radius, float rotRadians,
    int count)
{
    using namespace shape_impl;
    ShapeData sd = prepare(type);

    float cosR=cosf(rotRadians), sinR=sinf(rotRadians);

    // Cap unique positions so generation stays fast even at 100M particles;
    // the shader cycles via  id % targetCount  for the remainder.
    int genCount = std::min(count, 300'000);

    std::mt19937 rng(0xBEA75EEDu);
    std::uniform_real_distribution<float> uni(-1.f,1.f);
    std::uniform_real_distribution<float> jit(-0.35f,0.35f);

    std::vector<float> out;
    out.reserve((size_t)genCount*2);

    const int maxTries = genCount * 25;
    for (int i=0; i<maxTries && (int)out.size()/2 < genCount; ++i) {
        float lx=uni(rng), ly=uni(rng);
        if (!test(sd, lx, ly)) continue;
        float rx=lx*cosR - ly*sinR;
        float ry=lx*sinR + ly*cosR;
        out.push_back(cx + rx*radius + jit(rng));
        out.push_back(cy + ry*radius + jit(rng));
    }

    int filled=(int)out.size()/2;
    if (filled==0) {
        // Fallback: scatter at centre (shouldn't happen for valid shapes)
        for (int i=0;i<genCount;++i) { out.push_back(cx); out.push_back(cy); }
        filled=genCount;
    }

    // Cycle to fill remaining slots if rejection sampling fell short
    if (filled < genCount) {
        out.resize((size_t)genCount*2);
        for (int i=filled; i<genCount; ++i) {
            out[i*2]   = out[(i%filled)*2];
            out[i*2+1] = out[(i%filled)*2+1];
        }
    }

    return out;
}
