#version 430 core

in float vType;    // 0 = black hole,  1 = white hole
in float vRadius;

out vec4 fragColor;

void main() {
    vec2  coord = gl_PointCoord - 0.5;   // [-0.5, 0.5]
    float r     = length(coord) * 2.0;   // 0 at centre, 1 at edge
    if (r > 1.0) discard;

    // Glowing ring at ~75% of radius
    float ring = exp(-pow((r - 0.75) * 9.0, 2.0));
    // Soft core fill
    float core = pow(max(0.0, 1.0 - r * 1.5), 1.8);

    vec4 color;
    if (vType < 0.5) {
        // ── Black hole ────────────────────────────────────────────────────────
        // Dark interior, violet-blue glowing accretion ring
        vec3 ringColor = vec3(0.45, 0.15, 1.00);
        vec3 coreColor = vec3(0.02, 0.00, 0.05);
        float alpha    = ring * 0.95 + core * 0.6;
        color = vec4(ringColor * ring + coreColor * core, clamp(alpha, 0.0, 1.0));
    } else {
        // ── White hole ────────────────────────────────────────────────────────
        // Bright white-gold core, teal-blue ring
        vec3 coreColor = vec3(1.00, 0.97, 0.80);
        vec3 ringColor = vec3(0.30, 0.85, 1.00);
        float alpha    = core * 0.90 + ring * 0.70;
        color = vec4(coreColor * core + ringColor * ring, clamp(alpha, 0.0, 1.0));
    }

    fragColor = color;
}
