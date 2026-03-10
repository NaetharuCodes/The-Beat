#version 430 core

// ── Pull particle data from the SSBO ─────────────────────────────────────────
// Must match update.comp exactly: pos + vel + density + _pad = 24 bytes
struct Particle {
    vec2  pos;
    vec2  vel;
    float density;
    float _pad;
};
layout(std430, binding = 0) buffer Particles {
    Particle particles[];
};

uniform vec2  screenSize;
uniform float pointSize;
uniform float maxSpeed;

// ── Outputs to fragment shader ────────────────────────────────────────────────
out float vDensity;  // local particle density [0,1]  → drives colour
out float vSpeed;    // normalised speed [0,1]         → drives brightness

void main() {
    Particle p = particles[gl_VertexID];

    vDensity = p.density;
    vSpeed   = clamp(length(p.vel) / maxSpeed, 0.0, 1.0);

    // Convert pixel coords to NDC (OpenGL Y is up, screen Y is down)
    vec2 uv  = p.pos / screenSize;
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y    = -ndc.y;

    gl_Position  = vec4(ndc, 0.0, 1.0);
    gl_PointSize = pointSize;
}
