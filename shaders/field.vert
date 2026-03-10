#version 430 core

// ── Same noise functions as update.comp ──────────────────────────────────────
vec3 hash3(vec3 p) {
    p = vec3(
        dot(p, vec3(127.1, 311.7,  74.7)),
        dot(p, vec3(269.5, 183.3, 246.1)),
        dot(p, vec3(113.5, 271.9, 124.6))
    );
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(
        mix(mix(dot(hash3(i+vec3(0,0,0)),f-vec3(0,0,0)), dot(hash3(i+vec3(1,0,0)),f-vec3(1,0,0)), u.x),
            mix(dot(hash3(i+vec3(0,1,0)),f-vec3(0,1,0)), dot(hash3(i+vec3(1,1,0)),f-vec3(1,1,0)), u.x), u.y),
        mix(mix(dot(hash3(i+vec3(0,0,1)),f-vec3(0,0,1)), dot(hash3(i+vec3(1,0,1)),f-vec3(1,0,1)), u.x),
            mix(dot(hash3(i+vec3(0,1,1)),f-vec3(0,1,1)), dot(hash3(i+vec3(1,1,1)),f-vec3(1,1,1)), u.x), u.y),
        u.z);
}

vec2 curlNoise(vec3 p) {
    const float eps = 0.08;
    float dndy = noise3(p + vec3(0,eps,0)) - noise3(p - vec3(0,eps,0));
    float dndx = noise3(p + vec3(eps,0,0)) - noise3(p - vec3(eps,0,0));
    return vec2(dndy, -dndx) / (2.0 * eps);
}

// ── Uniforms ──────────────────────────────────────────────────────────────────
uniform vec2  screenSize;
uniform float noiseScale;
uniform float zOffset;
uniform float fieldStrength;  // same value used by the compute shader
uniform int   gridSpacing;    // pixels between arrow origins

// ── Outputs ───────────────────────────────────────────────────────────────────
out vec2  vUV;        // normalised screen pos → colour
out float vTip;       // 0 = tail, 1 = tip (for alpha ramp)
out float vMagnitude; // actual field magnitude at this arrow [0,1]

void main() {
    // Each arrow = 2 vertices: even = tail, odd = tip
    int arrowID = gl_VertexID / 2;
    int isTip   = gl_VertexID % 2;

    int cols = int(screenSize.x) / gridSpacing + 1;
    int col  = arrowID % cols;
    int row  = arrowID / cols;

    // Arrow origin (centre of grid cell)
    vec2 origin = vec2(col * gridSpacing, row * gridSpacing);
    vUV  = origin / screenSize;
    vTip = float(isTip);

    // Sample the curl field — do this for BOTH vertices so vMagnitude is consistent
    vec2  flow = curlNoise(vec3(origin * noiseScale, zOffset));
    float len  = length(flow);
    vec2  dir  = len > 0.001 ? flow / len : vec2(1.0, 0.0);

    // Arrow length scales with actual field magnitude AND fieldStrength.
    // curlNoise magnitude typically ranges 0..12; normalise by ~10 then scale.
    // At default fieldStrength=0.25 and average len≈5 → ~45% of gridSpacing.
    float arrowLen = clamp(len * fieldStrength * float(gridSpacing) * 0.36,
                           1.0, float(gridSpacing) * 0.90);

    // Normalised magnitude for the fragment shader (drives dimming)
    vMagnitude = clamp(len * fieldStrength * 0.25, 0.0, 1.0);

    vec2 pos = (isTip == 0) ? origin : origin + dir * arrowLen;

    // Pixel → NDC (flip Y to match screen convention)
    vec2 ndc = (pos / screenSize) * 2.0 - 1.0;
    ndc.y    = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
