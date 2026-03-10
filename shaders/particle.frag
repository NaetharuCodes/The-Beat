#version 430 core

in float vDensity;
in float vSpeed;

uniform float brightness;
uniform int   colorMode;   // 0=density (default) 1=fire 2=spectrum 3=cool
uniform float hueShift;    // 0-1, applied on top of any palette (audio-driven)

out vec4 fragColor;

// ── HSV ↔ RGB helpers ────────────────────────────────────────────────────────
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 rgb2hsv(vec3 c) {
    vec4 K  = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p  = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q  = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y)/(6.0*d + e)), d/(q.x + e), q.x);
}

// ── Colour palettes ───────────────────────────────────────────────────────────
// 0 – Density: deep indigo → electric cyan → near-white
vec3 paletteDensity(float t) {
    vec3 cold = vec3(0.04, 0.01, 0.22);
    vec3 mid  = vec3(0.00, 0.75, 1.00);
    vec3 hot  = vec3(1.00, 0.96, 0.88);
    vec3 a = mix(cold, mid, smoothstep(0.0, 0.5, t));
    return  mix(a,    hot,  smoothstep(0.5, 1.0, t));
}

// 1 – Fire: black → deep red → orange → yellow
vec3 paletteFire(float t) {
    vec3 c0 = vec3(0.0,  0.0,  0.0 );
    vec3 c1 = vec3(0.6,  0.0,  0.0 );
    vec3 c2 = vec3(1.0,  0.4,  0.0 );
    vec3 c3 = vec3(1.0,  0.95, 0.6 );
    float s = t * 3.0;
    if (s < 1.0) return mix(c0, c1, s);
    if (s < 2.0) return mix(c1, c2, s - 1.0);
    return             mix(c2, c3, s - 2.0);
}

// 2 – Spectrum: hue cycles with density (full rainbow)
vec3 paletteSpectrum(float t) {
    return hsv2rgb(vec3(t * 0.75, 0.9, 0.9));
}

// 3 – Cool: deep navy → electric blue → ice white
vec3 paletteCool(float t) {
    vec3 c0 = vec3(0.0,  0.02, 0.15);
    vec3 c1 = vec3(0.05, 0.35, 0.85);
    vec3 c2 = vec3(0.75, 0.95, 1.00);
    float s = t * 2.0;
    if (s < 1.0) return mix(c0, c1, s);
    return             mix(c1, c2, s - 1.0);
}

void main() {
    // ── Soft circular point sprite ────────────────────────────────────────────
    vec2  coord = gl_PointCoord - 0.5;
    float r     = length(coord) * 2.0;
    if (r > 1.0) discard;
    float alpha = pow(1.0 - r, 2.0);

    // ── Density ramp (gamma-expand for perceptual spread) ─────────────────────
    float t = pow(vDensity, 0.45);

    // ── Choose palette ────────────────────────────────────────────────────────
    vec3 color;
    if      (colorMode == 1) color = paletteFire    (t);
    else if (colorMode == 2) color = paletteSpectrum(t);
    else if (colorMode == 3) color = paletteCool     (t);
    else                     color = paletteDensity  (t);

    // Fast-moving particles get a white boost (kinetic hint)
    color = mix(color, vec3(1.0), vSpeed * vSpeed * 0.25);

    // Isolated particles fade into the void; clusters pop
    float vis = 0.08 + 0.92 * pow(vDensity + 0.05, 0.5);
    color *= vis;

    // ── Hue shift (audio-driven) ──────────────────────────────────────────────
    if (hueShift > 0.001 || hueShift < -0.001) {
        vec3 hsv = rgb2hsv(color);
        hsv.x    = fract(hsv.x + hueShift);
        color    = hsv2rgb(hsv);
    }

    fragColor = vec4(color, alpha * brightness);
}
