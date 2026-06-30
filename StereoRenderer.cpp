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
      quadVAO(0), quadVBO(0), cubeVAO(0), cubeVBO(0), overlayVAO(0), overlayVBO(0),
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
    // Buffer is empty at init; we upload line vertices each frame dynamically.
    glGenVertexArrays(1, &overlayVAO);
    glGenBuffers(1, &overlayVBO);
    glBindVertexArray(overlayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
    glBufferData(GL_ARRAY_BUFFER, 256 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
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

// ------------------------------------------------------------------ hologram
void StereoRenderer::renderHologram(const glm::mat4& arModel,
                                     const glm::mat4& arProj, float time) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);     // see inside the transparent faces

    glUseProgram(holoProgram);

    // Scale: slightly bigger than the marker for visual impact
    glm::mat4 model = glm::scale(arModel, glm::vec3(0.1f));

    glm::mat4 mvp = arProj * model;
    glUniformMatrix4fv(holoMVPLocation,  1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(holoTimeLocation,  time);
    // Cyan hologram color
    glm::vec3 cyan(0.05f, 0.85f, 1.0f);
    glUniform3fv(holoColorLocation, 1, glm::value_ptr(cyan));

    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // Second pass: bright wireframe outline for a "hard edge" glow effect
    glm::vec3 brightCyan(0.3f, 1.0f, 1.0f);
    glUniform3fv(holoColorLocation, 1, glm::value_ptr(brightCyan));
    glLineWidth(1.5f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glDisable(GL_BLEND);
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

// ------------------------------------------------------------------ gamer setup (Mode 3)
void StereoRenderer::renderGamerSetup(const glm::mat4& view,
                                       const glm::mat4& projection, float time) {
    // ---- compact lambdas ----
    auto cube = [&](glm::vec3 pos, glm::vec3 scale, glm::vec3 color) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
        m = glm::scale(m, scale);
        renderCube(m, view, projection, color);
    };
    auto rgb = [&](glm::vec3 pos, glm::vec3 scale, float hue) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
        m = glm::scale(m, scale);
        renderRGB(m, view, projection, time, hue);
    };

    // ---- color palette ----
    // Walls get a tiny purple tint so they are subtly visible (not pure black)
    const glm::vec3 cFloor  {0.04f, 0.02f, 0.07f};
    const glm::vec3 cWall   {0.05f, 0.02f, 0.09f};
    const glm::vec3 cDesk   {0.10f, 0.07f, 0.14f};  // dark carbon-purple
    const glm::vec3 cDevice {0.06f, 0.06f, 0.10f};  // near-black peripheral body
    const glm::vec3 cMon    {0.05f, 0.05f, 0.05f};  // monitor frame charcoal

    // =============================================
    //  ROOM  —  camera sits at y=1.2, z=+2
    //  All scene items face camera looking at -Z
    // =============================================
    cube({0.0f, -1.15f,  0.0f}, {14.f, 0.08f, 14.f}, cFloor);   // Floor
    cube({0.0f,  2.0f,  -6.5f}, {14.f, 6.0f,  0.10f}, cWall);   // Back wall
    cube({-7.0f, 2.0f,  -1.0f}, {0.10f,6.0f, 12.0f}, cWall);    // Left wall
    cube({ 7.0f, 2.0f,  -1.0f}, {0.10f,6.0f, 12.0f}, cWall);    // Right wall
    cube({0.0f,  5.0f,   0.0f}, {14.f, 0.08f, 14.f}, cFloor);   // Ceiling

    // =============================================
    //  DESK  —  y=-0.55 puts surface just below camera
    // =============================================
    cube({0.0f, -0.55f, -2.5f},  {4.8f, 0.08f, 2.4f}, cDesk);   // Desk surface
    // 4 thin metal legs
    cube({-2.3f, -0.98f, -1.6f}, {0.08f, 0.86f, 0.08f}, cDevice);
    cube({ 2.3f, -0.98f, -1.6f}, {0.08f, 0.86f, 0.08f}, cDevice);
    cube({-2.3f, -0.98f, -3.5f}, {0.08f, 0.86f, 0.08f}, cDevice);
    cube({ 2.3f, -0.98f, -3.5f}, {0.08f, 0.86f, 0.08f}, cDevice);

    // =============================================
    //  MONITOR
    //  Monitor center at y=0.55 (above desk surface), z=-3.8
    //  Frame: 2.0w x 1.3h; Screen inset
    // =============================================
    const float monZ = -3.8f;
    const float monY =  0.55f;
    cube({0.0f, monY, monZ},      {2.10f, 1.40f, 0.09f}, cMon);          // Outer frame
    cube({0.0f, monY, monZ+0.06f},{1.85f, 1.18f, 0.04f}, {0.01f,0.01f,0.01f}); // Bezel
    // Stand: column + base
    cube({0.0f, monY-0.78f, monZ},{0.11f, 0.55f, 0.11f}, cDevice);
    cube({0.0f, monY-1.04f, monZ},{0.60f, 0.05f, 0.38f}, cDevice);
    // Live camera feed on screen
    {
        glm::mat4 sm = glm::translate(glm::mat4(1.0f), {0.0f, monY, monZ+0.12f});
        sm = glm::scale(sm, {1.80f, 1.12f, 0.01f});
        renderTexturedQuad(sm, view, projection);
    }

    // =============================================
    //  PC TOWER  (right side of desk, on floor)
    // =============================================
    const float towerX = 3.0f;
    cube({towerX, -0.40f, -3.5f},  {0.55f, 1.50f, 0.55f}, cDevice);   // Body
    // Glass side panel (thin, very dark blue)
    cube({towerX-0.29f,-0.40f,-3.5f},{0.02f,1.36f,0.48f},{0.03f,0.03f,0.10f});

    // =============================================
    //  HEADPHONE STAND  (left of monitor on desk)
    // =============================================
    cube({-2.0f, -0.35f, -3.5f}, {0.08f, 0.95f, 0.08f}, cDevice);  // pole
    cube({-2.0f,  0.15f, -3.5f}, {0.55f, 0.06f, 0.22f}, cDevice);  // arch
    cube({-2.0f, -0.62f, -3.5f}, {0.50f, 0.05f, 0.32f}, cDevice);  // base

    // =============================================
    //  RGB OBJECTS
    // =============================================

    // 1. Desk front-edge RGB strip (the most visible accent, seen head-on)
    rgb({0.0f, -0.51f, -1.35f}, {4.8f, 0.08f, 0.015f}, 0.0f);

    // 2. LED underglow — thin horizontal line under desk, floor-facing
    rgb({0.0f, -0.60f, -2.5f},  {4.8f, 0.012f, 0.05f}, 0.05f);

    // 3. Ambilight halo behind monitor (3 strips: top, left side, right side)
    rgb({0.0f,  monY+0.82f, monZ-0.06f}, {2.30f, 0.025f, 0.08f}, 0.0f);  // top
    rgb({-(1.18f), monY, monZ-0.06f},    {0.025f,1.45f,  0.08f}, 0.0f);  // left
    rgb({ (1.18f), monY, monZ-0.06f},    {0.025f,1.45f,  0.08f}, 0.0f);  // right

    // 4. Keyboard — compact, sits on desk
    rgb({-0.3f, -0.50f, -2.15f}, {1.35f, 0.04f, 0.50f}, 0.33f);

    // 5. Mouse — small, to the right of keyboard
    rgb({ 0.95f, -0.51f, -2.10f},{0.15f, 0.05f, 0.25f}, 0.55f);

    // 6. PC tower: RGB front panel fan strip
    rgb({towerX-0.29f, -0.40f, -3.5f}, {0.02f, 1.10f, 0.42f}, 0.25f);
    // PC tower: top RGB accent
    rgb({towerX, 0.37f, -3.5f},  {0.55f, 0.020f, 0.55f}, 0.30f);

    // 7. Monitor stand base accent
    rgb({0.0f, monY-1.07f, monZ},{0.58f, 0.018f, 0.36f}, 0.70f);

    // 8. Headphone arch RGB band
    rgb({-2.0f, 0.20f, -3.5f},  {0.53f, 0.035f, 0.18f}, 0.62f);

    // =============================================
    //  NANOLEAF PANELS  —  LEFT WALL  (x ≈ -6.9)
    //  Panels face right (+X normal), clearly on wall,
    //  at various y/z positions forming a cluster.
    //  Using {depth, height, width} because panel is thin in X.
    // =============================================
    const float nX  = -6.85f;  // just in front of left wall
    const float nS  =  0.48f;  // panel face size
    const float nTh =  0.05f;  // panel thickness (X dimension)

    // Cluster: 3 columns x 4 rows (staggered)
    // Column A  z=-1.8
    rgb({nX, 0.20f, -1.80f}, {nTh, nS, nS}, 0.000f);
    rgb({nX, 0.72f, -1.80f}, {nTh, nS, nS}, 0.083f);
    rgb({nX, 1.24f, -1.80f}, {nTh, nS, nS}, 0.167f);
    rgb({nX, 1.76f, -1.80f}, {nTh, nS, nS}, 0.250f);
    // Column B  z=-2.40  (offset y by half a panel)
    rgb({nX, 0.46f, -2.40f}, {nTh, nS, nS}, 0.042f);
    rgb({nX, 0.98f, -2.40f}, {nTh, nS, nS}, 0.125f);
    rgb({nX, 1.50f, -2.40f}, {nTh, nS, nS}, 0.208f);
    rgb({nX, 2.02f, -2.40f}, {nTh, nS, nS}, 0.292f);
    // Column C  z=-3.00
    rgb({nX, 0.20f, -3.00f}, {nTh, nS, nS}, 0.333f);
    rgb({nX, 0.72f, -3.00f}, {nTh, nS, nS}, 0.417f);
    rgb({nX, 1.24f, -3.00f}, {nTh, nS, nS}, 0.500f);
    rgb({nX, 1.76f, -3.00f}, {nTh, nS, nS}, 0.583f);
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
