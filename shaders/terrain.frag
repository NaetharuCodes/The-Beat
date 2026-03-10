#version 430 core

in float vHeight;  // 0=valley, 1=peak

uniform float terrainAlpha;

out vec4 fragColor;

void main() {
    float t = vHeight;

    // ── Height-based colour (topographic map style) ───────────────────────────
    //   0.0 – 0.35  deep water → ocean blue
    //   0.35– 0.55  shoreline  → teal / seafoam
    //   0.55– 0.75  lowlands   → muted green
    //   0.75– 1.0   peaks      → grey-white
    vec3 col;
    if (t < 0.35)
        col = mix(vec3(0.02, 0.05, 0.25), vec3(0.00, 0.45, 0.55), t / 0.35);
    else if (t < 0.55)
        col = mix(vec3(0.00, 0.45, 0.55), vec3(0.10, 0.60, 0.35), (t - 0.35) / 0.20);
    else if (t < 0.75)
        col = mix(vec3(0.10, 0.60, 0.35), vec3(0.55, 0.65, 0.50), (t - 0.55) / 0.20);
    else
        col = mix(vec3(0.55, 0.65, 0.50), vec3(0.90, 0.93, 0.95), (t - 0.75) / 0.25);

    // Dim far lines slightly (already handled by perspective but extra subtlety)
    fragColor = vec4(col, terrainAlpha);
}
