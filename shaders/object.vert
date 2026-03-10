#version 430 core

// Gravity object SSBO — same layout as update.comp (binding = 1)
struct GravObj {
    vec2  pos;      // offset  0
    float type;     // offset  8
    float radius;   // offset 12
    float force;    // offset 16
    float _pad;     // offset 20
    vec2  vel;      // offset 24  (unused in render, present for layout match)
                    // total: 32 bytes
};
layout(std430, binding = 1) buffer GravityObjects {
    GravObj gravObjs[];
};

uniform vec2 screenSize;

out float vType;
out float vRadius;

void main() {
    GravObj obj = gravObjs[gl_VertexID];
    vType   = obj.type;
    vRadius = obj.radius;

    // Pixel → NDC (flip Y)
    vec2 ndc = (obj.pos / screenSize) * 2.0 - 1.0;
    ndc.y    = -ndc.y;

    gl_Position  = vec4(ndc, 0.0, 1.0);
    gl_PointSize = obj.radius * 2.0;   // point covers full influence radius
}
