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

// ------------------------------------------------------------------ renderCyberpunkLab
void StereoRenderer::renderCyberpunkLab(const glm::mat4& view,
                                        const glm::mat4& projection, float time) {
    // Compact helper lambda for solid cubes
    auto cube = [&](glm::vec3 pos, glm::vec3 sc, glm::vec3 col) {
        glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), pos), sc);
        renderCube(m, view, projection, col);
    };

    // Color Palette
    const glm::vec3 cFloor      {0.02f, 0.03f, 0.06f}; // Dark steel floor
    const glm::vec3 cWall       {0.01f, 0.01f, 0.03f}; // Deep space wall
    const glm::vec3 cConsole    {0.12f, 0.14f, 0.20f}; // Dark blue metal frame
    const glm::vec3 cHoloBlue    {0.00f, 0.70f, 1.00f}; // Neon cyan
    const glm::vec3 cHoloPurple  {0.60f, 0.20f, 0.90f}; // Cyber purple
    const glm::vec3 cServerRack  {0.08f, 0.09f, 0.12f}; // Server body
    const glm::vec3 cLedGreen    {0.00f, 1.00f, 0.40f};
    const glm::vec3 cLedRed      {1.00f, 0.15f, 0.20f};

    // ====================================================
    //  1. ENVIRONMENT (Floor, Bulkheads, Neon Grid)
    // ====================================================
    cube({0.0f, -1.15f, -3.0f}, {16.f, 0.08f, 16.f}, cFloor); // floor
    cube({0.0f,  3.50f, -3.0f}, {16.f, 0.08f, 16.f}, cWall);  // ceiling
    cube({-7.5f, 1.20f, -3.0f}, {0.08f, 5.0f, 16.f},  cWall);  // left wall
    cube({ 7.5f, 1.20f, -3.0f}, {0.08f, 5.0f, 16.f},  cWall);  // right wall
    cube({0.0f,  1.20f, -8.0f}, {16.f,  5.0f, 0.08f}, cWall);  // back wall

    // Floating structural beams on walls
    cube({-7.4f, 1.20f, -3.0f}, {0.04f, 0.15f, 16.0f}, cHoloBlue * 0.4f);
    cube({ 7.4f, 1.20f, -3.0f}, {0.04f, 0.15f, 16.0f}, cHoloBlue * 0.4f);

    // Floor grids (neon cian strips)
    cube({-3.0f, -1.12f, -3.0f}, {0.08f, 0.01f, 10.0f}, cHoloBlue * 0.7f);
    cube({ 3.0f, -1.12f, -3.0f}, {0.08f, 0.01f, 10.0f}, cHoloBlue * 0.7f);
    cube({ 0.0f, -1.12f, -5.0f}, {6.00f, 0.01f, 0.08f}, cHoloPurple * 0.7f);

    // ====================================================
    //  2. SERVER RACKS (With blinky lights flanking back wall)
    // ====================================================
    for (int i = 0; i < 4; ++i) {
        float x = (i < 2) ? -4.5f - i*1.2f : 3.3f + (i-2)*1.2f;
        float z = -6.5f;
        // Server cabinet body
        cube({x, 0.60f, z}, {0.80f, 3.40f, 0.80f}, cServerRack);

        // Blinking indicator LEDs (stacked vertically)
        for (int r = 0; r < 6; ++r) {
            float blink = 0.5f + 0.5f * std::sin(time * (5.0f + r) + x);
            glm::vec3 ledCol = (r % 3 == 0) ? cLedRed : ((r % 3 == 1) ? cLedGreen : cHoloBlue);
            cube({x - 0.32f, -0.60f + r*0.48f, z + 0.41f}, {0.06f, 0.06f, 0.03f}, ledCol * (blink > 0.4f ? 1.0f : 0.15f));
            cube({x + 0.32f, -0.60f + r*0.48f, z + 0.41f}, {0.06f, 0.06f, 0.03f}, ledCol * (blink > 0.6f ? 0.15f : 1.0f));
        }
    }

    // ====================================================
    //  3. HOLOGRAPHIC INCLINED PROJECTION TABLE
    // ====================================================
    const float tblX =  0.0f;
    const float tblY = -0.70f;
    const float tblZ = -3.5f;

    // Pedestal column
    cube({tblX, tblY, tblZ}, {0.55f, 0.85f, 0.55f}, cConsole);
    // Table surface plate (cyber hexagon shape simulated by overlapping boxes)
    cube({tblX, tblY + 0.42f, tblZ}, {1.60f, 0.08f, 1.20f}, cConsole * 0.8f);
    cube({tblX, tblY + 0.42f, tblZ}, {1.20f, 0.08f, 1.60f}, cConsole * 0.8f);

    // Glowing core in the center of the table (source of the hologram)
    cube({tblX, tblY + 0.47f, tblZ}, {0.28f, 0.04f, 0.28f}, cHoloBlue);

    // Concentric neon rings flat on the table
    glm::mat4 tblM = glm::translate(glm::mat4(1.0f), {tblX, tblY + 0.48f, tblZ});
    glm::mat4 mvpT = projection * view * tblM;
    drawOrbit3D(mvpT, 0.68f, cHoloBlue, 32, 2.0f);
    drawOrbit3D(mvpT, 0.48f, cHoloPurple, 24, 1.5f);

    // Dynamic scanning ring pulsing outwards from core
    float pulseT = std::fmod(time * 0.42f, 0.68f);
    drawOrbit3D(mvpT, pulseT, cHoloBlue * 0.7f, 32, 1.5f);

    // ====================================================
    //  4. TILTED FLOATING CAMERA SCREEN (HOLOGRAM FEED)
    //  Rotated backward around X-axis for floating look
    // ====================================================
    const float scrY = 0.55f;
    const float scrZ = -3.40f;
    const float sW = 2.45f, sH = 1.50f;

    // Glowing coordinate axes floating around screen
    cube({tblX - sW*0.62f, scrY, scrZ}, {0.02f, sH * 0.8f, 0.02f}, cHoloBlue * 0.6f);
    cube({tblX + sW*0.62f, scrY, scrZ}, {0.02f, sH * 0.8f, 0.02f}, cHoloPurple * 0.6f);

    // Tilted Model matrix: translation -> rotateX(-22 deg) -> scale
    glm::mat4 sm = glm::translate(glm::mat4(1.0f), {tblX, scrY, scrZ});
    sm = glm::rotate(sm, glm::radians(-22.0f), {1.0f, 0.0f, 0.0f});

    // Hologram bezel frame (cyan glass border)
    glm::mat4 frameM = glm::scale(sm, {sW + 0.12f, sH + 0.12f, 0.04f});
    renderCube(frameM, view, projection, cHoloBlue * 0.45f);

    // Additive projection cone from the table core to the screen
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    // Projection volume
    cube({tblX, tblY + 0.65f, tblZ + 0.05f}, {0.20f + (time*0.01f), 0.50f, 0.20f}, cHoloBlue * 0.08f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Live webcam feed screen projected inside the frame (rotated)
    {
        glm::mat4 screenM = glm::scale(sm, {sW, sH, 0.01f});
        renderTexturedQuad(screenM, view, projection);
    }
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
    float time = (float)glfwGetTime();

    // Compact helper lambda for solid cubes
    auto cube = [&](glm::vec3 pos, glm::vec3 sc, glm::vec3 col) {
        glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), pos), sc);
        renderCube(m, view, projection, col);
    };

    // Color Palette
    const glm::vec3 cSand      {0.84f, 0.69f, 0.44f}; // Warm desert sand
    const glm::vec3 cDarkSand  {0.78f, 0.63f, 0.38f}; // Shadowed dune sand
    const glm::vec3 cSky       {0.92f, 0.48f, 0.28f}; // Sunset orange sky
    const glm::vec3 cSun       {1.00f, 0.88f, 0.35f}; // Golden sun
    const glm::vec3 cWood      {0.33f, 0.21f, 0.11f}; // Dark rustic wood beams
    const glm::vec3 cStone     {0.45f, 0.44f, 0.46f}; // Weathered grey stone
    const glm::vec3 cLightStone{0.55f, 0.54f, 0.56f}; // Highlighted stone
    const glm::vec3 cGreen     {0.18f, 0.48f, 0.22f}; // Acacia flat foliage
    const glm::vec3 cWater     {0.08f, 0.52f, 0.68f}; // Blue oasis pool
    const glm::vec3 cCloud     {0.95f, 0.72f, 0.65f}; // Warm pink sunset clouds
    const glm::vec3 cCactus    {0.25f, 0.55f, 0.20f}; // Green cactus body
    const glm::vec3 cFireOrange{1.00f, 0.45f, 0.05f}; // Glowing fire orange
    const glm::vec3 cFireRed   {0.90f, 0.15f, 0.10f}; // Glowing fire red
    const glm::vec3 cTent      {0.75f, 0.65f, 0.50f}; // Bedouin tent canvas
    const glm::vec3 cDarkInside{0.15f, 0.12f, 0.10f}; // Tent inner darkness

    // ====================================================
    //  1. ENVIRONMENT & SKY (Floor, Sunset Sky, Sun)
    // ====================================================
    cube({0.0f, -1.15f, -3.0f}, {250.f, 0.08f, 250.f}, cSand); // Massively expanded floor to cover the entire horizon
    cube({0.0f,  3.20f, -14.8f}, {40.f, 10.0f, 0.08f}, cSky); // Wide back skybox wall
    
    // Golden sun low on horizon (pulsates slightly)
    float sunPulse = 1.0f + 0.03f * std::sin(time * 1.5f);
    cube({0.0f, 0.25f, -14.6f}, {2.80f * sunPulse, 2.80f * sunPulse, 0.05f}, cSun);

    // ====================================================
    //  2. TWINKLING STARS IN SKY
    // ====================================================
    for (int i = 0; i < 35; ++i) {
        float x = std::sin(float(i) * 142.12f) * 18.0f;
        float y = std::cos(float(i) * 85.43f) * 4.0f + 3.0f;
        float z = -14.2f;
        float twinkle = 0.3f + 0.7f * std::sin(time * 4.0f + float(i));
        cube({x, y, z}, {0.06f, 0.06f, 0.06f}, glm::vec3(0.9f, 0.95f, 1.0f) * twinkle);
    }

    // ====================================================
    //  3. THREE INTERCONNECTED OASIS WATER POOLS
    // ====================================================
    cube({ 0.0f, -1.11f, -2.5f}, {3.2f, 0.01f, 3.4f}, cWater);      // Central pool
    cube({-3.2f, -1.11f, -5.2f}, {2.0f, 0.01f, 2.0f}, cWater);      // Left secondary pool
    cube({-1.6f, -1.11f, -3.8f}, {1.4f, 0.01f, 1.4f}, cWater * 1.1f); // Connecting stream

    // ====================================================
    //  4. DRIFTING CLOUDS
    // ====================================================
    auto drawCloud = [&](float baseStartX, float y, float z, float speed, float sizeScale) {
        float x = baseStartX + std::fmod(time * speed, 30.0f);
        if (x > 15.0f) x = -15.0f + (x - 15.0f);
        cube({x, y, z}, {2.20f * sizeScale, 0.45f * sizeScale, 0.60f * sizeScale}, cCloud);
        cube({x + 0.40f * sizeScale, y + 0.20f * sizeScale, z}, {1.30f * sizeScale, 0.40f * sizeScale, 0.50f * sizeScale}, cCloud * 1.10f);
    };
    drawCloud(-8.0f,  3.60f, -14.0f, 0.20f, 1.20f);
    drawCloud( 5.0f,  4.20f, -14.0f, 0.12f, 0.95f);
    drawCloud(-12.0f, 2.80f, -14.0f, 0.25f, 1.35f);

    // ====================================================
    //  5. GEOMETRIC LOW-POLY SAND DUNES
    // ====================================================
    cube({-5.8f, -0.90f, -7.5f}, {6.0f, 0.65f, 6.5f}, cDarkSand);  // Left dune
    cube({ 5.8f, -0.85f, -8.0f}, {6.5f, 0.75f, 6.0f}, cDarkSand);  // Right dune
    cube({ 0.0f, -0.95f, -12.0f}, {10.0f, 0.58f, 3.8f}, cSand);    // Far center dune
    cube({-10.0f, -0.80f, -1.0f}, {7.0f, 0.50f, 8.0f}, cDarkSand); // Side left dune
    cube({ 10.0f, -0.80f, -1.0f}, {7.0f, 0.50f, 8.0f}, cDarkSand); // Side right dune

    // ====================================================
    //  6. PALM GROVES & ACACIA TREES
    // ====================================================
    auto drawAcacia = [&](glm::vec3 basePos, float scaleVal) {
        float trunkH = 1.50f * scaleVal;
        float trunkW = 0.12f * scaleVal;
        cube({basePos.x, basePos.y + trunkH*0.5f, basePos.z}, {trunkW, trunkH, trunkW}, cWood);
        cube({basePos.x, basePos.y + trunkH + 0.10f, basePos.z}, {1.40f*scaleVal, 0.18f*scaleVal, 1.40f*scaleVal}, cGreen);
        cube({basePos.x, basePos.y + trunkH + 0.28f, basePos.z}, {0.90f*scaleVal, 0.12f*scaleVal, 0.90f*scaleVal}, cGreen * 1.15f);
    };

    drawAcacia({-3.2f, -1.11f, -4.5f}, 1.10f); // Left tree near oasis
    drawAcacia({ 3.0f, -1.11f, -4.2f}, 1.05f); // Right tree
    drawAcacia({-5.2f, -1.11f, -7.0f}, 0.95f); // Far left tree
    drawAcacia({ 5.0f, -1.11f, -6.8f}, 0.90f); // Far right tree
    drawAcacia({-1.5f, -1.11f, -6.5f}, 1.00f); // Middle left tree

    // ====================================================
    //  7. SAGUARO CACTI
    // ====================================================
    auto drawCactus = [&](float cx, float cz, float hVal) {
        cube({cx, -1.11f + hVal*0.5f, cz}, {0.09f, hVal, 0.09f}, cCactus);
        cube({cx - 0.12f, -1.11f + hVal*0.55f, cz}, {0.16f, 0.08f, 0.08f}, cCactus);
        cube({cx - 0.20f, -1.11f + hVal*0.72f, cz}, {0.08f, hVal*0.35f, 0.08f}, cCactus);
        cube({cx + 0.12f, -1.11f + hVal*0.42f, cz}, {0.16f, 0.08f, 0.08f}, cCactus);
        cube({cx + 0.20f, -1.11f + hVal*0.58f, cz}, {0.08f, hVal*0.32f, 0.08f}, cCactus);
    };
    drawCactus(-2.0f, -2.5f, 0.85f);  // Close left cactus
    drawCactus( 2.2f, -2.8f, 0.95f);  // Close right cactus
    drawCactus( 5.5f, -5.0f, 1.20f);  // Far right cactus
    drawCactus(-6.5f, -3.5f, 1.05f);  // Left side dune cactus

    // ====================================================
    //  8. ANCIENT RUINS (Egyptian Obelisk & Broken Columns)
    // ====================================================
    // Standing Obelisk on the right
    const float obX = 4.2f, obZ = -5.0f;
    cube({obX, -0.90f, obZ}, {0.60f, 0.25f, 0.60f}, cStone);       // Plinth base
    cube({obX, -0.05f, obZ}, {0.35f, 1.50f, 0.35f}, cStone * 0.9f); // Tall shaft
    cube({obX,  0.75f, obZ}, {0.20f, 0.20f, 0.20f}, cLightStone);  // Pyramidion cap

    // Broken pillar lying in the sand on the left
    cube({-3.2f, -1.06f, -1.8f}, {1.40f, 0.26f, 0.26f}, cStone);
    cube({-2.4f, -1.06f, -1.8f}, {0.30f, 0.29f, 0.29f}, cLightStone); // ring detail

    // Weathered Rock Clusters around water pools
    cube({ 1.30f, -1.11f, -2.3f}, {0.24f, 0.16f, 0.22f}, cStone);
    cube({ 1.45f, -1.11f, -2.0f}, {0.18f, 0.10f, 0.18f}, cLightStone);
    cube({-1.35f, -1.11f, -2.1f}, {0.30f, 0.14f, 0.20f}, cStone);
    cube({-1.45f, -1.11f, -1.6f}, {0.16f, 0.08f, 0.16f}, cLightStone);
    cube({-3.80f, -1.11f, -5.5f}, {0.25f, 0.12f, 0.25f}, cStone);

    // ====================================================
    //  9. BEDOUIN TENT
    // ====================================================
    const float tentX = -2.5f, tentZ = -1.2f;
    // Main tent canvas body
    cube({tentX, -0.65f, tentZ}, {1.30f, 0.90f, 1.50f}, cTent);
    // Dark interior entry slit
    cube({tentX, -0.75f, tentZ + 0.74f}, {0.90f, 0.70f, 0.04f}, cDarkInside);
    // Wooden structural posts holding tent canopy
    cube({tentX - 0.60f, -0.65f, tentZ + 0.72f}, {0.05f, 0.90f, 0.05f}, cWood);
    cube({tentX + 0.60f, -0.65f, tentZ + 0.72f}, {0.05f, 0.90f, 0.05f}, cWood);

    // ====================================================
    //  10. CAMPFIRE
    // ====================================================
    const float fireX =  1.00f;
    const float fireZ = -1.20f;
    cube({fireX - 0.05f, -1.11f, fireZ}, {0.30f, 0.05f, 0.05f}, cWood);
    cube({fireX + 0.05f, -1.11f, fireZ}, {0.05f, 0.05f, 0.30f}, cWood);
    float fTime = time * 7.5f;
    float flameH = 0.12f + 0.06f * std::sin(fTime);
    cube({fireX, -1.05f + flameH*0.5f, fireZ}, {0.10f, flameH, 0.10f}, cFireOrange);
    cube({fireX, -1.02f + flameH*0.7f, fireZ}, {0.06f, flameH*0.6f, 0.06f}, cFireRed);
    // Glow
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    cube({fireX, -0.95f, fireZ}, {0.35f, 0.40f, 0.35f}, glm::vec3(0.25f, 0.10f, 0.01f));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // ====================================================
    //  11. FIREFLIES / OASIS MAGIC PARTICLES (Rising over pool)
    // ====================================================
    for (int i = 0; i < 6; ++i) {
        float fOffset = float(i) * 1.2f;
        float pTime = time * 0.7f + fOffset;
        float py = -1.08f + std::fmod(pTime, 1.3f);
        float px = std::sin(pTime * 2.5f) * 0.50f + (float(i - 3) * 0.40f);
        float pz = -2.50f + std::cos(pTime * 1.8f) * 0.50f;
        float scale = 0.026f * (1.3f - (py + 1.08f));
        cube({px, py, pz}, {scale, scale, scale}, cSun * 1.30f);
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
