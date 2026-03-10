#version 430 core

in vec2  vUV;
in float vTip;
in float vMagnitude;

uniform float fieldAlpha;   // overall opacity of the field overlay

out vec4 fragColor;

// Same palette as particle.frag — arrows match particle colours
vec3 palette(float t) {
    vec3 a = vec3(0.50, 0.50, 0.50);
    vec3 b = vec3(0.50, 0.50, 0.50);
    vec3 c = vec3(1.00, 0.70, 0.40);
    vec3 d = vec3(0.00, 0.15, 0.20);
    return a + b * cos(6.28318 * (c * t + d));
}

void main() {
    float t     = vUV.x * 0.6 + vUV.y * 0.4;
    vec3  color = palette(t);

    // Tail is dimmer, tip is brighter; weak-field arrows fade out entirely
    float alpha = mix(fieldAlpha * 0.35, fieldAlpha, vTip)
                  * (0.15 + 0.85 * vMagnitude);

    fragColor = vec4(color, alpha);
}
