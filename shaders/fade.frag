#version 430 core
uniform float alpha;    // how opaque the black overlay is (controls trail length)
out vec4 fragColor;
void main() {
    fragColor = vec4(0.0, 0.0, 0.0, alpha);
}
