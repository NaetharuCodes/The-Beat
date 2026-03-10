#pragma once
#include <glad/glad.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader {
public:
    GLuint id = 0;

    // ── Factories ─────────────────────────────────────────────────────────────
    static Shader fromFiles(const char* vertPath, const char* fragPath) {
        Shader s;
        std::string vs = readFile(vertPath);
        std::string fs = readFile(fragPath);
        if (vs.empty() || fs.empty()) return s;

        GLuint vert = compile(GL_VERTEX_SHADER,   vs.c_str());
        GLuint frag = compile(GL_FRAGMENT_SHADER, fs.c_str());
        s.id = link(vert, frag);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return s;
    }

    static Shader fromCompute(const char* compPath) {
        Shader s;
        std::string cs = readFile(compPath);
        if (cs.empty()) return s;

        GLuint comp = compile(GL_COMPUTE_SHADER, cs.c_str());
        s.id = glCreateProgram();
        glAttachShader(s.id, comp);
        glLinkProgram(s.id);
        checkLink(s.id, compPath);
        glDeleteShader(comp);
        return s;
    }

    // ── Usage ─────────────────────────────────────────────────────────────────
    void use() const { glUseProgram(id); }

    void setFloat(const char* n, float v)                        const { glUniform1f(loc(n), v); }
    void setInt  (const char* n, int   v)                        const { glUniform1i(loc(n), v); }
    void setVec2 (const char* n, float x, float y)              const { glUniform2f(loc(n), x, y); }
    void setVec3 (const char* n, float x, float y, float z)     const { glUniform3f(loc(n), x, y, z); }
    void setVec4 (const char* n, float x, float y, float z, float w) const { glUniform4f(loc(n), x, y, z, w); }

private:
    GLint loc(const char* n) const { return glGetUniformLocation(id, n); }

    static std::string readFile(const char* path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "[Shader] Cannot open: " << path << "\n";
            return "";
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static GLuint compile(GLenum type, const char* src) {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);
        GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]; glGetShaderInfoLog(id, sizeof(log), nullptr, log);
            std::cerr << "[Shader] Compile error:\n" << log << "\n";
        }
        return id;
    }

    static GLuint link(GLuint vert, GLuint frag) {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert);
        glAttachShader(prog, frag);
        glLinkProgram(prog);
        checkLink(prog, "vert+frag");
        return prog;
    }

    static void checkLink(GLuint prog, const char* name) {
        GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            std::cerr << "[Shader] Link error (" << name << "):\n" << log << "\n";
        }
    }
};
