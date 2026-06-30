#include "StereoRenderer.h"
#include <iostream>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// --- Vertex & Fragment Shaders as inline strings ---

// Background Quad Shader (For camera feed)
const std::string quadVertexShaderSrc = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTexCoords;
    out vec2 TexCoords;
    uniform mat4 uMVP;
    void main() {
        gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
        TexCoords = aTexCoords;
    }
)";

const std::string quadFragmentShaderSrc = R"(
    #version 330 core
    out vec4 FragColor;
    in vec2 TexCoords;
    uniform sampler2D videoTexture;
    void main() {
        // OpenCV captures in BGR format, swap channels to render correct RGB in OpenGL
        vec3 bgr = texture(videoTexture, TexCoords).rgb;
        FragColor = vec4(bgr.b, bgr.g, bgr.r, 1.0);
    }
)";

// 3D Object Shader (For rendering primitives)
const std::string objectVertexShaderSrc = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    uniform mat4 uMVP;
    void main() {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
)";

const std::string objectFragmentShaderSrc = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec3 uColor;
    void main() {
        FragColor = vec4(uColor, 1.0);
    }
)";

// ------------------------------------------------------------------
// HOLOGRAM Shader: animated sine-wave vertex deformation + scanlines
// ------------------------------------------------------------------
const std::string holoVertexShaderSrc = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    uniform mat4 uMVP;
    uniform float uTime;          // glfwGetTime() passed from CPU
    out vec3 vWorldPos;
    void main() {
        // Rhythmic vertical sine-wave deformation (the "dance" / floating effect)
        vec3 pos = aPos;
        float wave = sin(uTime * 4.0 + aPos.y * 8.0) * 0.04;
        pos.y += wave;
        pos.x += cos(uTime * 3.0 + aPos.y * 6.0) * 0.015;
        vWorldPos = pos;
        gl_Position = uMVP * vec4(pos, 1.0);
    }
)";

const std::string holoFragmentShaderSrc = R"(
    #version 330 core
    in vec3 vWorldPos;
    out vec4 FragColor;
    uniform float uTime;
    uniform vec3  uColor;         // base hologram color (e.g. cyan)
    void main() {
        // Horizontal scanlines: bright on even rows, dim on odd rows
        float scanline = 0.6 + 0.4 * sin(vWorldPos.y * 80.0 + uTime * 6.0);
        // Pulsating transparency
        float alpha = 0.55 + 0.2 * sin(uTime * 2.5);
        // Fresnel-like edge glow: brighter toward the outline
        float rim = pow(1.0 - abs(vWorldPos.z), 2.0);
        vec3 finalColor = uColor * scanline + vec3(rim * 0.4);
        FragColor = vec4(finalColor, alpha * scanline);
    }
)";

// ------------------------------------------------------------------
// 2-D Overlay Shader: flat screen-space lines (scan laser + reticles)
// ------------------------------------------------------------------
const std::string overlayVertexShaderSrc = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;   // NDC coords [-1..1]
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)";

const std::string overlayFragmentShaderSrc = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec4 uColor;   // RGBA
    void main() {
        FragColor = uColor;
    }
)";

// ------------------------------------------------------------------
// RGB Shader: HSV → RGB cycling per-object (Gamer Setup, Mode 3)
// ------------------------------------------------------------------
const std::string rgbVertexShaderSrc = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    uniform mat4 uMVP;
    void main() {
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
)";

// Proper HSV->RGB conversion in GLSL
const std::string rgbFragmentShaderSrc = R"(
    #version 330 core
    out vec4 FragColor;
    uniform float uTime;
    uniform float uHueOffset;

    vec3 hsv2rgb(vec3 c) {
        vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
        vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
        return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
    }

    void main() {
        float hue = fract(uTime * 0.12 + uHueOffset);
        vec3 rgb = hsv2rgb(vec3(hue, 1.0, 1.0));
        FragColor = vec4(rgb, 1.0);
    }
)";

StereoRenderer::StereoRenderer() 
    : quadProgram(0), objectProgram(0), holoProgram(0), overlayProgram(0), rgbProgram(0),
      quadVAO(0), quadVBO(0), cubeVAO(0), cubeVBO(0),
      overlayVAO(0), overlayVBO(0), ring3dVAO(0), ring3dVBO(0),
      videoTextureId(0),
      quadTexLocation(-1), quadMVPLocation(-1),
      objMVPLocation(-1), objColorLocation(-1),
      holoMVPLocation(-1), holoTimeLocation(-1), holoColorLocation(-1),
      overlayColorLocation(-1),
      rgbMVPLocation(-1), rgbTimeLocation(-1), rgbHueOffsetLocation(-1) {}

StereoRenderer::~StereoRenderer() {
    if (quadProgram)    glDeleteProgram(quadProgram);
    if (objectProgram)  glDeleteProgram(objectProgram);
    if (holoProgram)    glDeleteProgram(holoProgram);
    if (overlayProgram) glDeleteProgram(overlayProgram);
    if (rgbProgram)     glDeleteProgram(rgbProgram);
    if (quadVAO)    glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO)    glDeleteBuffers(1, &quadVBO);
    if (cubeVAO)    glDeleteVertexArrays(1, &cubeVAO);
    if (cubeVBO)    glDeleteBuffers(1, &cubeVBO);
    if (overlayVAO) glDeleteVertexArrays(1, &overlayVAO);
    if (overlayVBO) glDeleteBuffers(1, &overlayVBO);
    if (ring3dVAO)  glDeleteVertexArrays(1, &ring3dVAO);
    if (ring3dVBO)  glDeleteBuffers(1, &ring3dVBO);
    if (videoTextureId) glDeleteTextures(1, &videoTextureId);
}

bool StereoRenderer::initialize() {
    // Compile and link shaders
    GLuint quadVS = compileShader(GL_VERTEX_SHADER, quadVertexShaderSrc);
    GLuint quadFS = compileShader(GL_FRAGMENT_SHADER, quadFragmentShaderSrc);
    quadProgram = linkProgram(quadVS, quadFS);
    glDeleteShader(quadVS);
    glDeleteShader(quadFS);

    GLuint objVS = compileShader(GL_VERTEX_SHADER, objectVertexShaderSrc);
    GLuint objFS = compileShader(GL_FRAGMENT_SHADER, objectFragmentShaderSrc);
    objectProgram = linkProgram(objVS, objFS);
    glDeleteShader(objVS);
    glDeleteShader(objFS);

    // Compile hologram shader
    GLuint holoVS = compileShader(GL_VERTEX_SHADER,   holoVertexShaderSrc);
    GLuint holoFS = compileShader(GL_FRAGMENT_SHADER, holoFragmentShaderSrc);
    holoProgram   = linkProgram(holoVS, holoFS);
    glDeleteShader(holoVS);
    glDeleteShader(holoFS);

    // Compile 2-D overlay shader
    GLuint ovsVS  = compileShader(GL_VERTEX_SHADER,   overlayVertexShaderSrc);
    GLuint ovsFS  = compileShader(GL_FRAGMENT_SHADER, overlayFragmentShaderSrc);
    overlayProgram = linkProgram(ovsVS, ovsFS);
    glDeleteShader(ovsVS);
    glDeleteShader(ovsFS);

    // Compile RGB cycling shader (Mode 3 - Gamer Setup)
    GLuint rgbVS = compileShader(GL_VERTEX_SHADER,   rgbVertexShaderSrc);
    GLuint rgbFS = compileShader(GL_FRAGMENT_SHADER, rgbFragmentShaderSrc);
    rgbProgram   = linkProgram(rgbVS, rgbFS);
    glDeleteShader(rgbVS);
    glDeleteShader(rgbFS);

    if (!quadProgram || !objectProgram || !holoProgram || !overlayProgram || !rgbProgram) {
        std::cerr << "Failed to compile/link shader programs!" << std::endl;
        return false;
    }

    // Get uniform locations
    quadTexLocation    = glGetUniformLocation(quadProgram,    "videoTexture");
    quadMVPLocation    = glGetUniformLocation(quadProgram,    "uMVP");
    objMVPLocation     = glGetUniformLocation(objectProgram,  "uMVP");
    objColorLocation   = glGetUniformLocation(objectProgram,  "uColor");
    holoMVPLocation    = glGetUniformLocation(holoProgram,    "uMVP");
    holoTimeLocation   = glGetUniformLocation(holoProgram,    "uTime");
    holoColorLocation  = glGetUniformLocation(holoProgram,    "uColor");
    overlayColorLocation  = glGetUniformLocation(overlayProgram, "uColor");
    rgbMVPLocation        = glGetUniformLocation(rgbProgram,     "uMVP");
    rgbTimeLocation       = glGetUniformLocation(rgbProgram,     "uTime");
    rgbHueOffsetLocation  = glGetUniformLocation(rgbProgram,     "uHueOffset");

    // --- Setup Background Quad ---
    float quadVertices[] = {
        // Positions   // TexCoords (Inverted Y to display webcam feed right side up)
        -1.0f,  1.0f,  0.0f, 0.0f,
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,

        -1.0f,  1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Texture Coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // --- Setup 3D Cube ---
    float cubeVertices[] = {
        // Front face
        -0.5f, -0.5f,  0.5f,   0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,
        // Back face
        -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,   0.5f, -0.5f, -0.5f,  -0.5f, -0.5f, -0.5f,
        // Top face
        -0.5f,  0.5f, -0.5f,  -0.5f,  0.5f,  0.5f,   0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,   0.5f,  0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,
        // Bottom face
        -0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f,   0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,  -0.5f, -0.5f, -0.5f,
        // Right face
         0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,   0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,   0.5f, -0.5f,  0.5f,   0.5f, -0.5f, -0.5f,
        // Left face
        -0.5f, -0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,  -0.5f,  0.5f, -0.5f,  -0.5f, -0.5f, -0.5f
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // --- Setup Video Texture ---
    glGenTextures(1, &videoTextureId);
    glBindTexture(GL_TEXTURE_2D, videoTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // --- Setup Dynamic 2-D Overlay VAO (scan laser + reticles) ---
    glGenVertexArrays(1, &overlayVAO);
    glGenBuffers(1, &overlayVBO);
    glBindVertexArray(overlayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
    glBufferData(GL_ARRAY_BUFFER, 256 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // --- Setup 3-D Orbit Ring VAO (solar system, 3-float XYZ vertices) ---
    glGenVertexArrays(1, &ring3dVAO);
    glGenBuffers(1, &ring3dVBO);
    glBindVertexArray(ring3dVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ring3dVBO);
    glBufferData(GL_ARRAY_BUFFER, 128 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    return true;
}

void StereoRenderer::updateVideoTexture(const cv::Mat& frame) {
    if (frame.empty()) return;
    glBindTexture(GL_TEXTURE_2D, videoTextureId);
    // Upload OpenCV BGR texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame.cols, frame.rows, 0, GL_BGR, GL_UNSIGNED_BYTE, frame.data);
}

void StereoRenderer::renderBackground() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(quadProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, videoTextureId);
    glUniform1i(quadTexLocation, 0);

    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(quadMVPLocation, 1, GL_FALSE, glm::value_ptr(identity));

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void StereoRenderer::renderTexturedQuad(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection) {
    glUseProgram(quadProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, videoTextureId);
    glUniform1i(quadTexLocation, 0);

    glm::mat4 mvp = projection * view * model;
    glUniformMatrix4fv(quadMVPLocation, 1, GL_FALSE, glm::value_ptr(mvp));

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void StereoRenderer::renderCube(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color) {
    glUseProgram(objectProgram);
    glBindVertexArray(cubeVAO);

    glm::mat4 mvp = projection * view * model;
    glUniformMatrix4fv(objMVPLocation, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(objColorLocation, 1, glm::value_ptr(color));

    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

// ------------------------------------------------------------------ helpers
void StereoRenderer::drawLines2D(const std::vector<float>& verts,
                                  const glm::vec4& color, float lineWidth) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(lineWidth);

    glUseProgram(overlayProgram);
    glUniform4fv(overlayColorLocation, 1, glm::value_ptr(color));

    glBindVertexArray(overlayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
    glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size() / 2));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}


// ------------------------------------------------------------------ scan laser
void StereoRenderer::renderScanLaser(const std::vector<cv::Point2f>& corners2D,
                                      int frameW, int frameH, float time) {
    if (corners2D.size() < 4) return;

    // Bounding box of the 4 corners in pixel space
    float xmin = corners2D[0].x, xmax = corners2D[0].x;
    float ymin = corners2D[0].y, ymax = corners2D[0].y;
    for (auto& c : corners2D) {
        xmin = std::min(xmin, c.x);  xmax = std::max(xmax, c.x);
        ymin = std::min(ymin, c.y);  ymax = std::max(ymax, c.y);
    }

    // Pad the bounding box slightly for visual appeal
    float pad = (xmax - xmin) * 0.12f;
    xmin -= pad;  xmax += pad;
    ymin -= pad;  ymax += pad;

    // Animated Y position – sweeps from top to bottom and back using abs(sin)
    float t = std::abs(std::sin(time * 2.5f));
    float scanY = ymin + t * (ymax - ymin);

    // Convert pixel coords → NDC [-1, 1]
    auto toNDC_X = [&](float px) { return  2.0f * px / (float)frameW - 1.0f; };
    auto toNDC_Y = [&](float py) { return -2.0f * py / (float)frameH + 1.0f; };  // flip Y

    float ndcX0 = toNDC_X(xmin);
    float ndcX1 = toNDC_X(xmax);
    float ndcY  = toNDC_Y(scanY);

    // Line: two vertices (x0,y), (x1,y)
    std::vector<float> verts = { ndcX0, ndcY, ndcX1, ndcY };

    // Bright green laser line, slightly transparent
    drawLines2D(verts, glm::vec4(0.1f, 1.0f, 0.25f, 0.85f), 3.0f);
}

// ------------------------------------------------------------------ corner reticles
void StereoRenderer::renderCornerReticles(const std::vector<cv::Point2f>& corners2D,
                                           int frameW, int frameH) {
    if (corners2D.size() < 4) return;

    auto toNDC_X = [&](float px) { return  2.0f * px / (float)frameW - 1.0f; };
    auto toNDC_Y = [&](float py) { return -2.0f * py / (float)frameH + 1.0f; };

    // Size of the small crosshair arms (in NDC units)
    const float arm = 0.025f;

    // Colors cycling per corner: white, yellow, green, magenta
    glm::vec4 colors[4] = {
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        glm::vec4(1.0f, 0.95f, 0.2f, 1.0f),
        glm::vec4(0.3f, 1.0f, 0.4f, 1.0f),
        glm::vec4(1.0f, 0.3f, 0.9f, 1.0f)
    };

    for (int i = 0; i < 4; ++i) {
        float cx = toNDC_X(corners2D[i].x);
        float cy = toNDC_Y(corners2D[i].y);

        // Horizontal arm
        std::vector<float> hLine = { cx - arm, cy, cx + arm, cy };
        // Vertical arm
        std::vector<float> vLine = { cx, cy - arm, cx, cy + arm };

        drawLines2D(hLine, colors[i], 2.5f);
        drawLines2D(vLine, colors[i], 2.5f);
    }
}

// ------------------------------------------------------------------ drawOrbit3D
void StereoRenderer::drawOrbit3D(const glm::mat4& mvp, float radius,
                                  glm::vec3 color, int segs, float lineWidth) {
    const float PI = glm::pi<float>();
    std::vector<float> verts;
    verts.reserve(segs * 3);
    for (int i = 0; i < segs; ++i) {
        float theta = 2.0f * PI * (float)i / (float)segs;
        verts.push_back(radius * std::cos(theta));  // X
        verts.push_back(0.0f);                       // Y (circle in XZ plane)
        verts.push_back(radius * std::sin(theta));  // Z
    }

    glUseProgram(objectProgram);
    glUniformMatrix4fv(objMVPLocation, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3fv(objColorLocation,   1, glm::value_ptr(color));
    glLineWidth(lineWidth);

    glBindVertexArray(ring3dVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ring3dVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(verts.size() * sizeof(float)), verts.data());
    glDrawArrays(GL_LINE_LOOP, 0, segs);
    glBindVertexArray(0);
}

// ------------------------------------------------------------------ renderSolarSystem
void StereoRenderer::renderSolarSystem(const glm::mat4& arModel,
                                        const glm::mat4& arProj, float time) {
    // Identity view: arProj already encodes the calibrated camera matrix
    const glm::mat4 VIEW = glm::mat4(1.0f);

    // ---- Orbit definitions: {radius, tilt(deg), orbital speed, neon color, Saturn ring?} ----
    struct Orbit {
        float  radius;
        float  tiltDeg;
        float  speed;
        glm::vec3 color;
        bool   hasRing;
    };
    const Orbit orbits[3] = {
        {0.08f, 12.0f, 1.8f, {0.0f, 1.0f, 1.0f},  false},  // inner:  cyan
        {0.14f, 28.0f, 1.0f, {1.0f, 0.5f, 0.0f},  false},  // middle: orange
        {0.21f, 50.0f, 0.5f, {0.75f,0.2f, 1.0f},  true },  // outer:  purple/Saturn
    };

    // ====================================================
    // 1. ORBIT RINGS – drawn first so the star glow is on top
    // ====================================================
    for (const auto& orb : orbits) {
        // Tilt the ring plane around the X axis
        glm::mat4 tiltM = glm::rotate(glm::mat4(1.0f),
                                       glm::radians(orb.tiltDeg), {1,0,0});
        glm::mat4 mvp   = arProj * arModel * tiltM;

        // Bright ring pass
        drawOrbit3D(mvp, orb.radius, orb.color, 64, 2.0f);

        // Wide soft-glow pass (additive blend)
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        drawOrbit3D(mvp, orb.radius, orb.color * 0.20f, 64, 5.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // ====================================================
    // 2. CENTRAL STAR – concentric glow cubes, additive blend
    // ====================================================
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);

    struct GlowLayer { float sc; glm::vec3 col; };
    const GlowLayer starLayers[] = {
        {0.18f, {0.04f, 0.02f, 0.00f}},
        {0.11f, {0.16f, 0.08f, 0.01f}},
        {0.065f,{0.50f, 0.28f, 0.02f}},
        {0.038f,{1.00f, 0.80f, 0.20f}},
        {0.020f,{1.00f, 1.00f, 0.90f}},
    };
    // Star "breathes" with a slow sine pulse
    float pulse = 1.0f + 0.18f * std::sin(time * 5.2f);
    for (const auto& sl : starLayers) {
        glm::mat4 sm = glm::scale(arModel, glm::vec3(sl.sc * pulse));
        renderCube(sm, VIEW, arProj, sl.col);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // ====================================================
    // 3. PLANETS – animated along their orbits
    // ====================================================
    for (int i = 0; i < 3; ++i) {
        const Orbit& orb = orbits[i];
        float angle = time * orb.speed;
        float r     = orb.radius;
        float tilt  = glm::radians(orb.tiltDeg);

        // Compute position on the tilted ellipse
        float px     = r * std::cos(angle);
        float pzFlat = r * std::sin(angle);
        float py     = pzFlat * std::sin(tilt);
        float pz     = pzFlat * std::cos(tilt);
        glm::vec3 ppos(px, py, pz);

        // Solid planet body
        glm::mat4 pm = glm::scale(glm::translate(arModel, ppos), glm::vec3(0.022f));
        renderCube(pm, VIEW, arProj, orb.color);

        // Planet glow (additive, bigger)
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        renderCube(glm::scale(glm::translate(arModel, ppos), glm::vec3(0.055f)),
                   VIEW, arProj, orb.color * 0.18f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        // Saturn rings on the outer planet (3 concentric orbit circles)
        if (orb.hasRing) {
            glm::mat4 satBase = glm::translate(arModel, ppos);
            // Tilt Saturn's disk 30° for visibility
            satBase = glm::rotate(satBase, glm::radians(30.0f), {0, 0, 1});
            for (float dr : {0.030f, 0.038f, 0.048f}) {
                drawOrbit3D(arProj * satBase, dr, orb.color, 48, 1.5f);
            }
        }
    }
}

// ------------------------------------------------------------------ renderSpaceshipBridge
void StereoRenderer::renderSpaceshipBridge(const glm::mat4& view,
                                           const glm::mat4& projection, float time) {
    // Compact helper lambda for solid cubes
    auto cube = [&](glm::vec3 pos, glm::vec3 sc, glm::vec3 col) {
        glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), pos), sc);
        renderCube(m, view, projection, col);
    };

    // Color definitions
    const glm::vec3 cMetalFloor   {0.07f, 0.08f, 0.12f}; // dark blue-steel flooring
    const glm::vec3 cMetalWall    {0.05f, 0.05f, 0.08f}; // metallic panels
    const glm::vec3 cConsoleBody  {0.14f, 0.16f, 0.22f}; // control panel carbon/metal
    const glm::vec3 cWindshield   {0.08f, 0.11f, 0.15f}; // frame borders
    const glm::vec3 cHoloBlue     {0.00f, 0.70f, 1.00f}; // primary sci-fi cyan
    const glm::vec3 cHoloGreen    {0.00f, 1.00f, 0.40f}; // radar/status green
    const glm::vec3 cButtonRed    {0.90f, 0.10f, 0.20f};
    const glm::vec3 cButtonYellow {0.95f, 0.75f, 0.05f};

    // ====================================================
    //  1. STARFIELD (Drawn behind the windshield screen)
    //  Deterministic layout using std::sin/cos so they stay fixed
    // ====================================================
    for (int i = 0; i < 40; ++i) {
        float x = std::sin(float(i) * 342.12f) * 12.0f;
        float y = std::cos(float(i) * 115.43f) * 5.0f + 1.2f;
        float z = -8.0f - std::abs(std::sin(float(i) * 874.32f)) * 8.0f;
        // Blinking animation
        float blink = 0.4f + 0.6f * std::sin(time * 3.5f + float(i));
        cube({x, y, z}, {0.08f, 0.08f, 0.08f}, glm::vec3(0.9f, 0.95f, 1.0f) * blink);
    }

    // ====================================================
    //  2. BRIDGE CABIN / STRUCTURAL ROOM
    // ====================================================
    cube({0.0f, -1.15f, -3.0f}, {16.f, 0.08f, 16.f}, cMetalFloor); // Floor
    cube({0.0f,  3.50f, -3.0f}, {16.f, 0.08f, 16.f}, cMetalWall);  // Ceiling
    cube({-7.8f, 1.20f, -3.0f}, {0.10f, 5.0f, 16.f},  cMetalWall);  // Left bulkhead
    cube({ 7.8f, 1.20f, -3.0f}, {0.10f, 5.0f, 16.f},  cMetalWall);  // Right bulkhead
    // Front window frame supports
    cube({-2.30f,  1.20f, -4.6f}, {0.12f, 4.80f, 0.12f}, cWindshield);
    cube({ 2.30f,  1.20f, -4.6f}, {0.12f, 4.80f, 0.12f}, cWindshield);
    cube({ 0.00f,  2.80f, -4.6f}, {5.20f, 0.12f, 0.12f}, cWindshield);
    cube({ 0.00f, -0.65f, -4.6f}, {5.20f, 0.12f, 0.12f}, cWindshield);

    // Diagonal support frames for cockpit aesthetic
    {
        glm::mat4 mDiagL = glm::translate(glm::mat4(1.0f), {-3.5f, 1.8f, -4.0f});
        mDiagL = glm::rotate(mDiagL, glm::radians(35.0f), {0, 0, 1});
        mDiagL = glm::scale(mDiagL, {0.10f, 3.20f, 0.10f});
        renderCube(mDiagL, view, projection, cWindshield);

        glm::mat4 mDiagR = glm::translate(glm::mat4(1.0f), {3.5f, 1.8f, -4.0f});
        mDiagR = glm::rotate(mDiagR, glm::radians(-35.0f), {0, 0, 1});
        mDiagR = glm::scale(mDiagR, {0.10f, 3.20f, 0.10f});
        renderCube(mDiagR, view, projection, cWindshield);
    }

    // ====================================================
    //  3. MAIN SCREEN FRAME + LIVE WEBCAM FEED (WINDSHEILD)
    // ====================================================
    const float scrY = 1.05f;
    const float scrZ = -4.5f;
    const float sW = 4.2f, sH = 2.4f;

    // Glowing border frame
    cube({0.0f, scrY, scrZ}, {sW + 0.18f, sH + 0.18f, 0.08f}, cWindshield);
    // Dark glass underlay
    cube({0.0f, scrY, scrZ + 0.04f}, {sW + 0.02f, sH + 0.02f, 0.04f}, {0.01f, 0.01f, 0.03f});
    // Live webcam feed projected on the main viewscreen
    {
        glm::mat4 sm = glm::scale(
            glm::translate(glm::mat4(1.0f), {0.0f, scrY, scrZ + 0.08f}),
            {sW - 0.06f, sH - 0.06f, 0.01f});
        renderTexturedQuad(sm, view, projection);
    }

    // Neon ambient back-light under console edge
    cube({0.0f, -0.68f, -4.2f}, {5.0f, 0.03f, 0.08f}, cHoloBlue);

    // ====================================================
    //  4. COMMAND CONSOLES (Left, center and right)
    // ====================================================
    const float conY = -0.75f;
    const float conZ = -3.2f;

    // Center Console
    cube({0.0f, conY, conZ}, {1.8f, 0.40f, 1.2f}, cConsoleBody);
    // Left console desk (tilted panel)
    cube({-2.3f, conY, conZ}, {1.5f, 0.40f, 1.2f}, cConsoleBody);
    // Right console desk (tilted panel)
    cube({2.3f, conY, conZ}, {1.5f, 0.40f, 1.2f}, cConsoleBody);

    // Floor neon strip panels (run from the back to the front)
    cube({-4.5f, -1.12f, -3.0f}, {0.15f, 0.01f, 12.0f}, cHoloBlue);
    cube({ 4.5f, -1.12f, -3.0f}, {0.15f, 0.01f, 12.0f}, cHoloBlue);

    // ====================================================
    //  5. STATUS BUTTONS / KEYBOARD INDICATORS
    // ====================================================
    // Left desk buttons
    cube({-2.0f, conY + 0.22f, conZ - 0.2f}, {0.12f, 0.03f, 0.12f}, cButtonRed);
    cube({-2.4f, conY + 0.22f, conZ - 0.2f}, {0.12f, 0.03f, 0.12f}, cHoloGreen);
    cube({-2.2f, conY + 0.22f, conZ - 0.4f}, {0.12f, 0.03f, 0.12f}, cHoloBlue);
    // Center control panel buttons/sliders
    for (int i = 0; i < 4; ++i) {
        float x = -0.5f + i * 0.35f;
        cube({x, conY + 0.22f, conZ - 0.3f}, {0.08f, 0.03f, 0.14f}, cButtonYellow);
        cube({x, conY + 0.22f, conZ - 0.1f}, {0.08f, 0.03f, 0.08f}, (i % 2 == 0) ? cButtonRed : cHoloBlue);
    }
    // Right desk keyboard array
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 2; ++row) {
            float rx = 1.9f + col * 0.22f;
            float rz = -3.4f + row * 0.22f;
            cube({rx, conY + 0.21f, rz}, {0.10f, 0.015f, 0.10f}, cHoloBlue * (0.4f + 0.6f * std::cos(time * 2.0f + float(col + row))));
        }
    }

    // ====================================================
    //  6. HOLOGRAPHIC PULSING RADAR SCREEN
    //  Flat concentric rings pulsing dynamically on console
    // ====================================================
    // Left console radar
    glm::mat4 radML = glm::translate(glm::mat4(1.0f), {-2.4f, conY + 0.21f, conZ});
    radML = glm::rotate(radML, glm::radians(5.0f), {1, 0, 0}); // tilt console surface angle
    glm::mat4 mvpL = projection * view * radML;
    drawOrbit3D(mvpL, 0.30f, cHoloGreen, 32, 1.5f);
    drawOrbit3D(mvpL, 0.12f, cHoloGreen, 24, 1.0f);
    float pulseL = std::fmod(time * 0.28f, 0.30f);
    drawOrbit3D(mvpL, pulseL, cHoloGreen * 0.6f, 32, 2.0f);

    // Right console secondary scanner
    glm::mat4 radMR = glm::translate(glm::mat4(1.0f), {2.4f, conY + 0.21f, conZ - 0.2f});
    radMR = glm::rotate(radMR, glm::radians(5.0f), {1, 0, 0});
    glm::mat4 mvpR = projection * view * radMR;
    drawOrbit3D(mvpR, 0.25f, cHoloBlue, 28, 1.5f);
    float pulseR = std::fmod(time * 0.35f, 0.25f);
    drawOrbit3D(mvpR, pulseR, cHoloBlue * 0.7f, 28, 2.0f);
}

// ------------------------------------------------------------------ rgb helper
void StereoRenderer::renderRGB(const glm::mat4& model, const glm::mat4& view,
                                const glm::mat4& projection,
                                float time, float hueOffset) {
    glUseProgram(rgbProgram);
    glm::mat4 mvp = projection * view * model;
    glUniformMatrix4fv(rgbMVPLocation,       1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(rgbTimeLocation,       time);
    glUniform1f(rgbHueOffsetLocation,  hueOffset);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}



void StereoRenderer::renderVirtualScene(const glm::mat4& view, const glm::mat4& projection) {
    // Render a large dark grey floor plane
    glm::mat4 floorModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    floorModel = glm::scale(floorModel, glm::vec3(30.0f, 0.1f, 30.0f));
    renderCube(floorModel, view, projection, glm::vec3(0.2f, 0.2f, 0.2f));

    // Render a virtual gallery of columns around the user
    std::vector<glm::vec3> positions = {
        glm::vec3(-4.0f, 0.0f, -4.0f),
        glm::vec3(4.0f, 0.0f, -4.0f),
        glm::vec3(-4.0f, 0.0f,  4.0f),
        glm::vec3(4.0f, 0.0f,  4.0f),
        glm::vec3(0.0f, 1.0f, -7.0f),
        glm::vec3(-6.0f, 0.5f, 0.0f),
        glm::vec3(6.0f, 0.5f,  0.0f)
    };

    std::vector<glm::vec3> colors = {
        glm::vec3(0.9f, 0.2f, 0.2f), // Red
        glm::vec3(0.2f, 0.8f, 0.2f), // Green
        glm::vec3(0.2f, 0.2f, 0.9f), // Blue
        glm::vec3(0.9f, 0.9f, 0.2f), // Yellow
        glm::vec3(0.9f, 0.2f, 0.9f), // Purple
        glm::vec3(0.2f, 0.9f, 0.9f), // Cyan
        glm::vec3(0.9f, 0.5f, 0.1f)  // Orange
    };

    for (size_t i = 0; i < positions.size(); ++i) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), positions[i]);
        model = glm::scale(model, glm::vec3(0.6f, 2.0f, 0.6f));
        renderCube(model, view, projection, colors[i]);
    }
}

void StereoRenderer::renderStereo(int windowWidth, int windowHeight, float eyeSeparation,
                                  const glm::mat4& baseView, const glm::mat4& projection,
                                  const std::function<void(const glm::mat4&, const glm::mat4&)>& renderSceneCallback) {
    int halfWidth = windowWidth / 2;

    // --- Left Eye Viewport ---
    glViewport(0, 0, halfWidth, windowHeight);
    // Shift camera base position to the left (moves the world to the right in view space)
    glm::mat4 leftView = glm::translate(glm::mat4(1.0f), glm::vec3(eyeSeparation / 2.0f, 0.0f, 0.0f)) * baseView;
    renderSceneCallback(leftView, projection);

    // --- Right Eye Viewport ---
    glViewport(halfWidth, 0, halfWidth, windowHeight);
    // Shift camera base position to the right (moves the world to the left in view space)
    glm::mat4 rightView = glm::translate(glm::mat4(1.0f), glm::vec3(-eyeSeparation / 2.0f, 0.0f, 0.0f)) * baseView;
    renderSceneCallback(rightView, projection);
}

// --- Shader Helper functions implementation ---

GLuint StereoRenderer::compileShader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "Error compiling shader (type " << type << "): " << infoLog << std::endl;
        return 0;
    }
    return shader;
}

GLuint StereoRenderer::linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "Error linking program: " << infoLog << std::endl;
        return 0;
    }
    return program;
}
