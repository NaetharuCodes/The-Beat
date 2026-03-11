#pragma once
#include <glad/glad.h>
#include <vector>
#include <random>
#include <algorithm>
#include <iostream>
#include <cmath>
#include "shader.h"
#include "image_target.h"

// ── Params ────────────────────────────────────────────────────────────────────
struct Params {
    // Simulation
    int   count          = 100000;
    float noiseScale     = 0.0025f;
    float fieldStrength  = 0.25f;
    float evolutionSpeed = 0.05f;
    float maxSpeed       = 3.5f;
    float damping        = 0.97f;
    float particleMass   = 0.0f;
    bool  collisionEnabled = false;
    float collisionRadius  = 8.0f;
    float densityRadius    = 20.0f;

    // Rendering
    float trailFade  = 0.04f;
    float pointSize  = 2.0f;
    float brightness = 0.75f;
    int   colorMode  = 0;       // 0=density 1=fire 2=spectrum 3=cool
    float hueShift   = 0.0f;    // 0-1, cycles hue (audio-driven)

    // Mouse
    bool  mouseActive = false;
    float mouseRadius = 200.0f;
    float mouseForce  = 3.0f;
    int   mouseMode   = 1;

    // Image targeting
    float targetStrength  = 0.0f;   // current blend [0,1]
    float targetDamping   = 0.92f;  // extra damping when snapping to image
    float blendInTime     = 1.5f;   // seconds for image to fully form
    float blendOutTime    = 1.0f;   // seconds to release back to flow

    // Audio reactivity knobs (0 = off, 1 = full effect, >1 = exaggerated)
    float reactBeatField  = 1.2f;   // beat → field strength burst
    float reactBeatSize   = 1.0f;   // beat → point size pulse + speed
    float reactBeatKick   = 1.5f;   // beat → direct velocity impulse (most physical)
    float reactBassSize   = 0.8f;   // bass energy → point size
    float reactMidColor   = 1.2f;   // mid energy → hue shift speed
    float reactHighEvol   = 0.8f;   // high energy → evolution speed

    // Swarm / Boids
    float swarmSeparation = 0.0f;   // repulsion between close particles
    float swarmAlignment  = 0.0f;   // velocity matching with neighbours
    float swarmCohesion   = 0.0f;   // pull toward group centre
    float swarmRadius     = 60.0f;  // neighbourhood radius in screen pixels

    // Gravity objects
    bool  objectsDrift      = true;
    bool  gravityMode       = false;
    float gravG             = 80.0f;
    float gravSoftening     = 40.0f;
    bool  particleGravEnabled   = false;
    float particleGravG         = 0.3f;
    float particleGravSoftening = 20.0f;
};

// Must match update.comp and object.vert (std430, 32 bytes)
struct GravityObject {
    float x, y;
    float type;
    float radius;
    float force;
    float _pad = 0.0f;
    float vx = 0.0f, vy = 0.0f;
};

// ── ParticleSystem ────────────────────────────────────────────────────────────
class ParticleSystem {
public:
    Params params;
    std::vector<GravityObject> gravObjects;

    GLuint fbo    = 0;
    GLuint fboTex = 0;
    int    screenW = 0, screenH = 0;

    // ── Init ──────────────────────────────────────────────────────────────────
    void init(int w, int h) {
        screenW = w; screenH = h;
        loadShaders();
        buildQuadBuffers();
        buildFBO(w, h);
        buildParticleBuffers(params.count);
        buildGravBuffer();
        buildDummyTargetBuffer();
        clearTrail();
    }

    void resize(int w, int h) {
        screenW = w; screenH = h;
        destroyFBO();
        buildFBO(w, h);
        clearTrail();
    }

    void reloadShaders() {
        glDeleteProgram(computeShader.id); computeShader.id = 0;
        glDeleteProgram(renderShader.id);  renderShader.id  = 0;
        glDeleteProgram(fadeShader.id);    fadeShader.id    = 0;
        glDeleteProgram(screenShader.id);  screenShader.id  = 0;
        glDeleteProgram(objectShader.id);  objectShader.id  = 0;
        loadShaders();
    }

    // ── Simulation step ───────────────────────────────────────────────────────
    void update(float dt, float mouseX, float mouseY,
                float audioFieldMult = 1.0f, float audioSpeedMult = 1.0f,
                float audioBeatKick = 0.0f)
    {
        zOffset += params.evolutionSpeed * dt;

        computeShader.use();
        computeShader.setFloat("dt",            dt);
        computeShader.setFloat("zOffset",       zOffset);
        computeShader.setFloat("noiseScale",    params.noiseScale);
        computeShader.setFloat("fieldStrength", params.fieldStrength);
        computeShader.setFloat("maxSpeed",      params.maxSpeed);
        computeShader.setFloat("damping",       std::pow(params.damping, dt * 60.0f));
        computeShader.setVec2 ("screenSize",    (float)screenW, (float)screenH);
        computeShader.setVec2 ("mousePos",      mouseX, mouseY);
        computeShader.setFloat("mouseRadius",   params.mouseRadius);
        computeShader.setFloat("mouseForce",    params.mouseActive ? params.mouseForce : 0.0f);
        computeShader.setInt  ("mouseMode",     params.mouseMode);
        computeShader.setInt  ("particleCount", activeCount);
        computeShader.setFloat("particleMass",  params.particleMass);
        computeShader.setInt  ("collisionEnabled", params.collisionEnabled ? 1 : 0);
        computeShader.setFloat("collisionRadius",  params.collisionRadius);
        computeShader.setFloat("densityRadius",    params.densityRadius);
        computeShader.setInt  ("gravObjCount",  (int)gravObjects.size());
        computeShader.setInt  ("gravMode",      params.gravityMode ? 1 : 0);
        computeShader.setFloat("gravSoftening", params.gravSoftening);
        computeShader.setFloat("gravG",         params.gravG);
        computeShader.setInt  ("particleGravEnabled",   params.particleGravEnabled ? 1 : 0);
        computeShader.setFloat("particleGravG",          params.particleGravG);
        computeShader.setFloat("particleGravSoftening",  params.particleGravSoftening);
        computeShader.setFloat("targetStrength", params.targetStrength);
        computeShader.setInt  ("targetCount",    activeTargetCount);
        computeShader.setFloat("targetDamping",  params.targetDamping);
        computeShader.setFloat("audioFieldMult",  audioFieldMult);
        computeShader.setFloat("audioSpeedMult",  audioSpeedMult);
        computeShader.setFloat("beatKick",        audioBeatKick);
        computeShader.setFloat("swarmSeparation", params.swarmSeparation);
        computeShader.setFloat("swarmAlignment",  params.swarmAlignment);
        computeShader.setFloat("swarmCohesion",   params.swarmCohesion);
        computeShader.setFloat("swarmRadius",     params.swarmRadius);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gravSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, targetSSBO);
        glDispatchCompute((activeCount + 255) / 256, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        // Move gravity objects on CPU
        if (!gravObjects.empty()) {
            float damp = std::pow(params.damping, dt * 60.0f);
            bool moved = false;
            if (params.gravityMode) {
                for (size_t i = 0; i < gravObjects.size(); i++)
                    for (size_t j = i+1; j < gravObjects.size(); j++) {
                        float dx = gravObjects[j].x - gravObjects[i].x;
                        float dy = gravObjects[j].y - gravObjects[i].y;
                        float d2 = dx*dx + dy*dy + params.gravSoftening*params.gravSoftening;
                        float d  = sqrtf(d2);
                        float ai = params.gravG * gravObjects[j].force / d2 * dt * 60.0f;
                        float aj = params.gravG * gravObjects[i].force / d2 * dt * 60.0f;
                        gravObjects[i].vx += (dx/d)*ai; gravObjects[i].vy += (dy/d)*ai;
                        gravObjects[j].vx -= (dx/d)*aj; gravObjects[j].vy -= (dy/d)*aj;
                    }
                moved = true;
            } else if (params.objectsDrift) {
                for (auto& obj : gravObjects) {
                    float cx, cy;
                    cpuCurlNoise(obj.x*params.noiseScale, obj.y*params.noiseScale, zOffset, cx, cy);
                    float accel = params.fieldStrength / std::max(obj.force, 0.5f);
                    obj.vx += cx * accel * dt * 60.0f;
                    obj.vy += cy * accel * dt * 60.0f;
                }
                moved = true;
            }
            if (moved) {
                for (auto& obj : gravObjects) {
                    obj.vx *= damp; obj.vy *= damp;
                    float spd = sqrtf(obj.vx*obj.vx + obj.vy*obj.vy);
                    float cap = params.maxSpeed * 0.5f;
                    if (spd > cap) { obj.vx = obj.vx/spd*cap; obj.vy = obj.vy/spd*cap; }
                    obj.x = fmodf(obj.x + obj.vx * dt * 60.0f + screenW, (float)screenW);
                    obj.y = fmodf(obj.y + obj.vy * dt * 60.0f + screenH, (float)screenH);
                }
                uploadGravBuffer();
            }
        }
    }

    // ── Rendering ─────────────────────────────────────────────────────────────
    void drawToFBO() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, screenW, screenH);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        fadeShader.use();
        fadeShader.setFloat("alpha", params.trailFade);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        renderShader.use();
        renderShader.setVec2 ("screenSize", (float)screenW, (float)screenH);
        renderShader.setFloat("pointSize",  params.pointSize);
        renderShader.setFloat("brightness", params.brightness);
        renderShader.setFloat("maxSpeed",   params.maxSpeed);
        renderShader.setInt  ("colorMode",  params.colorMode);
        renderShader.setFloat("hueShift",   params.hueShift);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
        glBindVertexArray(particleVAO);
        glDrawArrays(GL_POINTS, 0, activeCount);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void blitToScreen() {
        glDisable(GL_BLEND);
        screenShader.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fboTex);
        screenShader.setInt("screenTexture", 0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    void drawObjects() {
        if (gravObjects.empty()) return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_PROGRAM_POINT_SIZE);
        objectShader.use();
        objectShader.setVec2("screenSize", (float)screenW, (float)screenH);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gravSSBO);
        glBindVertexArray(particleVAO);
        glDrawArrays(GL_POINTS, 0, (int)gravObjects.size());
        glDisable(GL_BLEND);
    }

    void clearTrail() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDisable(GL_BLEND);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resetParticles() { buildParticleBuffers(params.count); clearTrail(); }
    void setCount(int n)  { params.count = n; buildParticleBuffers(n); }

    // ── Image target upload ───────────────────────────────────────────────────
    void setTargetSSBO(GLuint handle, int count) {
        targetSSBO        = handle;
        activeTargetCount = count;
    }
    void clearTargetSSBO() {
        targetSSBO        = dummyTargetSSBO;
        activeTargetCount = 0;
        params.targetStrength = 0.0f;
    }

    // ── Gravity objects ───────────────────────────────────────────────────────
    void addGravObject(float x, float y, float type, float radius, float force) {
        gravObjects.push_back({x, y, type, radius, force, 0.0f, 0.0f, 0.0f});
        uploadGravBuffer();
    }
    void clearGravObjects() { gravObjects.clear(); uploadGravBuffer(); }

    void cleanup() {
        if (ssbo)            glDeleteBuffers(1, &ssbo);
        if (gravSSBO)        glDeleteBuffers(1, &gravSSBO);
        if (dummyTargetSSBO) glDeleteBuffers(1, &dummyTargetSSBO);
        if (particleVAO)     glDeleteVertexArrays(1, &particleVAO);
        if (quadVAO)         glDeleteVertexArrays(1, &quadVAO);
        if (quadVBO)         glDeleteBuffers(1, &quadVBO);
        destroyFBO();
    }

private:
    Shader computeShader, renderShader, fadeShader, screenShader, objectShader;
    GLuint ssbo            = 0;
    GLuint gravSSBO        = 0;
    GLuint targetSSBO      = 0;
    GLuint dummyTargetSSBO = 0;
    GLuint particleVAO     = 0;
    GLuint quadVAO         = 0;
    GLuint quadVBO         = 0;
    int    activeCount       = 0;
    int    activeTargetCount = 0;
    float  zOffset           = 0.0f;

    void loadShaders() {
        computeShader = Shader::fromCompute("shaders/update.comp");
        renderShader  = Shader::fromFiles  ("shaders/particle.vert", "shaders/particle.frag");
        fadeShader    = Shader::fromFiles  ("shaders/quad.vert",     "shaders/fade.frag");
        screenShader  = Shader::fromFiles  ("shaders/quad.vert",     "shaders/screen.frag");
        objectShader  = Shader::fromFiles  ("shaders/object.vert",   "shaders/object.frag");
    }

    void buildQuadBuffers() {
        float verts[] = { -1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1 };
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    void buildFBO(int w, int h) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &fboTex);
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "[FBO] Incomplete!\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroyFBO() {
        if (fbo)    { glDeleteFramebuffers(1, &fbo);  fbo    = 0; }
        if (fboTex) { glDeleteTextures(1, &fboTex);   fboTex = 0; }
    }

    void buildParticleBuffers(int count) {
        activeCount = count;
        std::vector<float> data(count * 6, 0.0f);
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> rx(0.0f, (float)screenW);
        std::uniform_real_distribution<float> ry(0.0f, (float)screenH);
        std::uniform_real_distribution<float> rv(-0.5f, 0.5f);
        for (int i = 0; i < count; ++i) {
            data[i*6+0] = rx(rng); data[i*6+1] = ry(rng);
            data[i*6+2] = rv(rng); data[i*6+3] = rv(rng);
        }
        if (ssbo)        glDeleteBuffers(1, &ssbo);
        if (particleVAO) glDeleteVertexArrays(1, &particleVAO);
        glGenBuffers(1, &ssbo);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     (GLsizeiptr)(data.size()*sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glGenVertexArrays(1, &particleVAO);
    }

    void buildDummyTargetBuffer() {
        float dummy[2] = {0.0f, 0.0f};
        glGenBuffers(1, &dummyTargetSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, dummyTargetSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(dummy), dummy, GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        targetSSBO = dummyTargetSSBO;
    }

    void buildGravBuffer() {
        if (gravSSBO) glDeleteBuffers(1, &gravSSBO);
        glGenBuffers(1, &gravSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gravSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     128*sizeof(GravityObject), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void uploadGravBuffer() {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gravSSBO);
        if (!gravObjects.empty())
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            gravObjects.size()*sizeof(GravityObject), gravObjects.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    static float cpuNoise3(float px, float py, float pz) {
        auto frc  = [](float v){ return v - floorf(v); };
        auto dot3 = [](float ax,float ay,float az,float bx,float by,float bz){
            return ax*bx+ay*by+az*bz; };
        auto h3 = [&](float hx,float hy,float hz,float& ox,float& oy,float& oz){
            float x=dot3(hx,hy,hz,127.1f,311.7f, 74.7f);
            float y=dot3(hx,hy,hz,269.5f,183.3f,246.1f);
            float z=dot3(hx,hy,hz,113.5f,271.9f,124.6f);
            ox=-1.0f+2.0f*frc(sinf(x)*43758.5453123f);
            oy=-1.0f+2.0f*frc(sinf(y)*43758.5453123f);
            oz=-1.0f+2.0f*frc(sinf(z)*43758.5453123f);
        };
        float ix=floorf(px),iy=floorf(py),iz=floorf(pz);
        float fx=px-ix,fy=py-iy,fz=pz-iz;
        auto sm=[](float t){ return t*t*t*(t*(t*6.f-15.f)+10.f); };
        float ux=sm(fx),uy=sm(fy),uz=sm(fz);
        float ox,oy,oz;
        auto dat=[&](float cx,float cy,float cz){
            h3(ix+cx,iy+cy,iz+cz,ox,oy,oz);
            return dot3(ox,oy,oz,fx-cx,fy-cy,fz-cz);
        };
        auto mx=[](float a,float b,float t){ return a+(b-a)*t; };
        return mx(mx(mx(dat(0,0,0),dat(1,0,0),ux),mx(dat(0,1,0),dat(1,1,0),ux),uy),
                  mx(mx(dat(0,0,1),dat(1,0,1),ux),mx(dat(0,1,1),dat(1,1,1),ux),uy),uz);
    }
    static void cpuCurlNoise(float px,float py,float pz,float& cx,float& cy){
        const float eps=0.08f;
        float dndy=cpuNoise3(px,py+eps,pz)-cpuNoise3(px,py-eps,pz);
        float dndx=cpuNoise3(px+eps,py,pz)-cpuNoise3(px-eps,py,pz);
        cx=dndy/(2.0f*eps); cy=-dndx/(2.0f*eps);
    }
};
