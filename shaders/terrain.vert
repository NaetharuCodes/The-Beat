#version 430 core

// ── Grid dimensions (determines total line vertex count) ──────────────────────
uniform int   terrainCols;   // columns (left↔right resolution)
uniform int   terrainRows;   // rows    (near↔far  resolution)

// ── Visual controls ───────────────────────────────────────────────────────────
uniform vec2  screenSize;  // pixel dimensions — needed to match particle noise space
uniform float terrainAmp;  // height amplitude in world units
uniform float noiseScale;  // identical to the particle compute shader uniform
uniform float zOffset;     // identical time offset used by the particle field
uniform float horizonY;    // NDC Y where the horizon sits (default ~0.2)

out float vHeight;  // normalised [0,1]: 0=valley, 1=peak → drives colour

// ── Noise — identical to update.comp so terrain matches the flow field ────────
vec3 hash3(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7,  74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return mix(
        mix(mix(dot(hash3(i+vec3(0,0,0)), f-vec3(0,0,0)),
                dot(hash3(i+vec3(1,0,0)), f-vec3(1,0,0)), u.x),
            mix(dot(hash3(i+vec3(0,1,0)), f-vec3(0,1,0)),
                dot(hash3(i+vec3(1,1,0)), f-vec3(1,1,0)), u.x), u.y),
        mix(mix(dot(hash3(i+vec3(0,0,1)), f-vec3(0,0,1)),
                dot(hash3(i+vec3(1,0,1)), f-vec3(1,0,1)), u.x),
            mix(dot(hash3(i+vec3(0,1,1)), f-vec3(0,1,1)),
                dot(hash3(i+vec3(1,1,1)), f-vec3(1,1,1)), u.x), u.y),
        u.z);
}

void main() {
    int cols = terrainCols;
    int rows = terrainRows;

    // ── Decode gl_VertexID into a grid position ───────────────────────────────
    // Layout: horizontal segments first, then vertical segments.
    //   Horizontal: (rows+1) rows × cols segments × 2 verts = (rows+1)*cols*2
    //   Vertical:   (cols+1) cols × rows segments × 2 verts = (cols+1)*rows*2
    int hVerts = (rows + 1) * cols * 2;
    float col_frac, row_frac;

    if (gl_VertexID < hVerts) {
        int segIdx   = gl_VertexID / 2;
        int endpoint = gl_VertexID % 2;   // 0=left vertex, 1=right vertex
        int row = segIdx / cols;
        int seg = segIdx % cols;
        col_frac = float(seg + endpoint) / float(cols);
        row_frac = float(row)            / float(rows);
    } else {
        int idx2     = gl_VertexID - hVerts;
        int segIdx   = idx2 / 2;
        int endpoint = idx2 % 2;           // 0=near vertex, 1=far vertex
        int col = segIdx / rows;
        int seg = segIdx % rows;
        col_frac = float(col)            / float(cols);
        row_frac = float(seg + endpoint) / float(rows);
    }

    // ── World-space position ──────────────────────────────────────────────────
    // row_frac=0 → near (bottom of screen), row_frac=1 → far (horizon)
    float worldX = (col_frac - 0.5) * 6.0;           // -3 … +3 world units
    float worldZ = mix(0.5, 14.0, row_frac);          //  0.5(near) … 14(far)

    // Three independent noise axes:
    //   X → lateral position  (same pixel-space scale as particles)
    //   Y → depth into terrain (spatial variation front-to-back)
    //   Z → pure scroll driven by zOffset  ← this is what moves the landscape
    //
    // zOffset accumulates at evolutionSpeed/sec; ×4 gives comfortable scroll speed
    // while keeping it locked to the same rate the particle field evolves.
    float pxPerUnit = screenSize.x / 6.0;      // world unit → pixel-space factor
    float sampleX = worldX * pxPerUnit * noiseScale;
    float sampleY = worldZ * pxPerUnit * noiseScale;
    float sampleZ = zOffset * 4.0;
    float height  = noise3(vec3(sampleX, sampleY, sampleZ)) * terrainAmp;

    vHeight = clamp(height / terrainAmp * 0.5 + 0.5, 0.0, 1.0);

    // ── Perspective projection ────────────────────────────────────────────────
    // Camera at (0, 1.0, 0) looking in +Z.
    // At infinite distance every point converges to NDC (0, 0) → shifted to horizonY.
    const float eyeH = 1.0;
    const float fov  = 1.2;

    float px = (worldX         / worldZ) * fov;
    float py = ((height - eyeH)/ worldZ) * fov + horizonY;

    gl_Position = vec4(px, py, 0.0, 1.0);
}
