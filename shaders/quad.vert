#version 430 core
layout(location = 0) in vec2 pos;  // NDC position (-1 to 1)

out vec2 vTexCoord;

void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    vTexCoord   = pos * 0.5 + 0.5;   // remap NDC → UV [0,1]
}
