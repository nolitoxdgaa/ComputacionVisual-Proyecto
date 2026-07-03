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
        vec3 rgb = texture(videoTexture, TexCoords).rgb;
        FragColor = vec4(rgb, 1.0);
    }
)";

// 3D Object Shader (For rendering primitives)
// 3D Object Shader (For rendering primitives)
const std::string objectVertexShaderSrc = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    uniform mat4 uMVP;
    uniform mat4 uModel;
    out vec3 vLocalPos;
    out vec3 vWorldPos;
    void main() {
        vLocalPos = aPos;
        vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
        gl_Position = uMVP * vec4(aPos, 1.0);
    }
)";

const std::string objectFragmentShaderSrc = R"(
    #version 330 core
    in vec3 vLocalPos;
    in vec3 vWorldPos;
    out vec4 FragColor;
    uniform vec3 uColor;
    uniform mat4 uModel;
    void main() {
        // Check if the object color matches any self-luminous objects (Sun, Stars, Flames)
        bool isLuminous = (uColor.r > 0.98 && uColor.g > 0.85 && uColor.b < 0.40)   // Sun (1.00, 0.88, 0.35)
                       || (uColor.r > 0.88 && uColor.g > 0.93 && uColor.b > 0.98)   // Stars (0.90, 0.95, 1.00)
                       || (uColor.r > 0.99 && uColor.g > 0.40 && uColor.b < 0.10)   // Fire Orange (1.00, 0.45, 0.05)
                       || (uColor.r > 0.88 && uColor.g < 0.20 && uColor.b < 0.15)   // Fire Red (0.90, 0.15, 0.10)
                       || (uColor.r < 0.10 && uColor.g > 0.78 && uColor.b > 0.98);  // Fire Cyan (0.00, 0.80, 1.00)

        float factor = 1.0;
        if (!isLuminous) {
            // Analytical local normal for unit cube
            vec3 localNormal = vec3(0.0, 1.0, 0.0);
            vec3 absPos = abs(vLocalPos);
            float eps = 0.49;
            if (absPos.x > eps) {
                localNormal = vec3(sign(vLocalPos.x), 0.0, 0.0);
            } else if (absPos.y > eps) {
                localNormal = vec3(0.0, sign(vLocalPos.y), 0.0);
            } else if (absPos.z > eps) {
                localNormal = vec3(0.0, 0.0, sign(vLocalPos.z));
            }

            // Transform normal to world space using the model matrix
            vec3 worldNormal = normalize(mat3(uModel) * localNormal);

            // Sunset sun light direction (matches the setting sun)
            vec3 lightDir = normalize(vec3(0.0, 0.35, -1.0));
            float diff = max(dot(worldNormal, lightDir), 0.0);

            // Ambient lighting
            float ambient = 0.38;
            factor = ambient + diff * 0.62;
        }

        vec3 finalColor = uColor * factor;
        FragColor = vec4(finalColor, 1.0);
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
      objMVPLocation(-1), objColorLocation(-1), objModelLocation(-1),
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
    objModelLocation   = glGetUniformLocation(objectProgram,  "uModel");
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
    
    // Corregir el formato de colores de BGR a RGB para evitar el tono azulado en OpenGL
    cv::Mat rgbFrame;
    cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
    
    glBindTexture(GL_TEXTURE_2D, videoTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rgbFrame.cols, rgbFrame.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, rgbFrame.data);
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
    glUniformMatrix4fv(objModelLocation, 1, GL_FALSE, glm::value_ptr(model));
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
// ------------------------------------------------------------------ renderCyberpunkLab
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
    //  1. ENVIRONMENT (Fully Enclosed Room Box)
    // ====================================================
    cube({0.0f, -1.15f, -2.0f}, {20.f, 0.08f, 20.f}, cFloor); // floor
    cube({0.0f,  3.80f, -2.0f}, {20.f, 0.08f, 20.f}, cWall);  // ceiling
    cube({-9.5f, 1.30f, -2.0f}, {0.08f, 5.0f, 20.f},  cWall);  // left wall
    cube({ 9.5f, 1.30f, -2.0f}, {0.08f, 5.0f, 20.f},  cWall);  // right wall
    cube({0.0f,  1.30f, -10.5f}, {20.f,  5.0f, 0.08f}, cWall);  // back wall (front of player)
    cube({0.0f,  1.30f,  6.5f}, {20.f,  5.0f, 0.08f}, cWall);  // front wall (behind player)

    // Floor & Ceiling grid neon lines
    cube({-4.0f, -1.12f, -2.0f}, {0.06f, 0.01f, 16.0f}, cHoloBlue * 0.5f);
    cube({ 4.0f, -1.12f, -2.0f}, {0.06f, 0.01f, 16.0f}, cHoloBlue * 0.5f);
    cube({0.0f,  3.75f, -2.0f}, {0.08f, 0.02f, 16.0f}, cHoloPurple * 0.5f);
    cube({0.0f,  3.75f, -2.0f}, {16.0f, 0.02f, 0.08f}, cHoloBlue * 0.5f);

    // Structural corner columns
    cube({-9.2f, 1.30f, -10.2f}, {0.5f, 5.0f, 0.5f}, cConsole * 0.7f);
    cube({ 9.2f, 1.30f, -10.2f}, {0.5f, 5.0f, 0.5f}, cConsole * 0.7f);
    cube({-9.2f, 1.30f,   6.2f}, {0.5f, 5.0f, 0.5f}, cConsole * 0.7f);
    cube({ 9.2f, 1.30f,   6.2f}, {0.5f, 5.0f, 0.5f}, cConsole * 0.7f);

    // ====================================================
    //  2. SERVER RACK DRAWING HELPER
    // ====================================================
    auto drawServer = [&](glm::vec3 pos, glm::vec3 sc, float rotationY = 0.0f) {
        // Server body
        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
        if (rotationY != 0.0f) {
            m = glm::rotate(m, glm::radians(rotationY), {0.0f, 1.0f, 0.0f});
        }
        m = glm::scale(m, sc);
        renderCube(m, view, projection, cServerRack);

        // Blinking indicator LEDs on local front face (+Z of server orientation)
        for (int r = 0; r < 5; ++r) {
            float blink = 0.5f + 0.5f * std::sin(time * (4.0f + r) + pos.x + pos.z);
            glm::vec3 ledCol = (r % 3 == 0) ? cLedRed : ((r % 3 == 1) ? cLedGreen : cHoloBlue);
            glm::vec3 ledScale = {0.05f, 0.05f, 0.02f};
            
            glm::vec3 ledPosL = {-sc.x*0.35f, -sc.y*0.35f + r*sc.y*0.17f, sc.z*0.51f};
            glm::vec3 ledPosR = { sc.x*0.35f, -sc.y*0.35f + r*sc.y*0.17f, sc.z*0.51f};

            // Left LED
            glm::mat4 mL = glm::translate(glm::mat4(1.0f), pos);
            if (rotationY != 0.0f) mL = glm::rotate(mL, glm::radians(rotationY), {0.0f, 1.0f, 0.0f});
            renderCube(glm::scale(glm::translate(mL, ledPosL), ledScale), view, projection, ledCol * (blink > 0.4f ? 1.0f : 0.15f));
            // Right LED
            renderCube(glm::scale(glm::translate(mL, ledPosR), ledScale), view, projection, ledCol * (blink > 0.6f ? 0.15f : 1.0f));
        }
    };

    // ====================================================
    //  3. 360-DEGREE SERVER POPULATION
    // ====================================================
    // Back wall racks (in front of player)
    drawServer({-4.5f, 0.60f, -7.5f}, {0.90f, 3.40f, 0.90f});
    drawServer({-5.7f, 0.60f, -7.5f}, {0.90f, 3.40f, 0.90f});
    drawServer({ 5.6f, 0.60f, -7.5f}, {0.90f, 3.40f, 0.90f});
    drawServer({ 6.8f, 0.60f, -7.5f}, {0.90f, 3.40f, 0.90f});

    // Left wall racks (turned 90 deg)
    drawServer({-8.8f, 0.60f, -5.0f}, {0.90f, 3.40f, 0.90f}, 90.0f);
    drawServer({-8.8f, 0.60f, -2.5f}, {0.90f, 3.40f, 0.90f}, 90.0f);
    drawServer({-8.8f, 0.60f,  0.0f}, {0.90f, 3.40f, 0.90f}, 90.0f);

    // Right wall racks (turned -90 deg)
    drawServer({ 8.8f, 0.60f, -5.0f}, {0.90f, 3.40f, 0.90f}, -90.0f);
    drawServer({ 8.8f, 0.60f, -2.5f}, {0.90f, 3.40f, 0.90f}, -90.0f);
    drawServer({ 8.8f, 0.60f,  0.0f}, {0.90f, 3.40f, 0.90f}, -90.0f);

    // Front wall racks (behind player, turned 180 deg)
    drawServer({-4.0f, 0.60f,  5.8f}, {0.90f, 3.40f, 0.90f}, 180.0f);
    drawServer({ 4.0f, 0.60f,  5.8f}, {0.90f, 3.40f, 0.90f}, 180.0f);

    // ====================================================
    //  4. SCI-FI AIRLOCK WINDOW/DOOR (Behind player)
    // ====================================================
    const float bX_door = 0.0f, bZ_door = 6.2f;
    // Central glass view-panel
    cube({bX_door, 0.60f, bZ_door}, {2.4f, 3.4f, 0.05f}, glm::vec3(0.04f, 0.08f, 0.15f));
    // Door neon borders
    cube({bX_door - 1.22f, 0.60f, bZ_door - 0.02f}, {0.05f, 3.4f, 0.06f}, cHoloBlue);
    cube({bX_door + 1.22f, 0.60f, bZ_door - 0.02f}, {0.05f, 3.4f, 0.06f}, cHoloBlue);
    cube({bX_door,         2.32f, bZ_door - 0.02f}, {2.50f, 0.05f, 0.06f}, cHoloBlue);

    // ====================================================
    //  5. CASCADES OF MATRIX CODE (All around)
    // ====================================================
    auto drawCodeColumn = [&](float cx, float cz, float speedOffset) {
        float speed = 2.0f + speedOffset;
        float fall = std::fmod(time * speed, 8.0f);
        for (int r = 0; r < 7; ++r) {
            float y = 3.6f - fall + r * 0.45f;
            if (y < -1.1f || y > 3.6f) continue;
            float brightness = 1.0f - (r * 0.14f);
            cube({cx, y, cz}, {0.05f, 0.14f, 0.05f}, glm::vec3(0.0f, 1.0f, 0.40f) * brightness);
        }
    };
    // Front wall code streams
    drawCodeColumn(-3.0f, -10.0f, 0.2f);
    drawCodeColumn( 3.0f, -10.0f, -0.4f);
    // Left wall code streams
    drawCodeColumn(-9.0f, -2.5f, 0.5f);
    drawCodeColumn(-9.0f,  2.0f, -0.2f);
    // Right wall code streams
    drawCodeColumn( 9.0f, -2.5f, 0.1f);
    drawCodeColumn( 9.0f,  2.0f, -0.5f);
    // Behind wall code streams
    drawCodeColumn(-1.8f,  6.0f, 0.3f);
    drawCodeColumn( 1.8f,  6.0f, -0.1f);

    // ====================================================
    //  6. GLOWING SCI-FI COMMAND CONSOLE DESK (Front)
    // ====================================================
    const float dX = 0.0f, dY = -0.70f, dZ = -2.40f;
    // Desk Surface
    cube({dX, dY, dZ}, {6.5f, 0.08f, 1.4f}, cConsole);
    // Support pillars
    cube({dX - 2.5f, -0.92f, dZ}, {0.4f, 0.4f, 1.0f}, cConsole * 0.7f);
    cube({dX + 2.5f, -0.92f, dZ}, {0.4f, 0.4f, 1.0f}, cConsole * 0.7f);

    // Glowing Neon Command Panels on Desk
    cube({dX - 1.6f, dY + 0.05f, dZ + 0.1f}, {1.2f, 0.02f, 0.5f}, cConsole * 0.5f);
    for (int k = 0; k < 6; ++k) {
        cube({dX - 2.0f + k*0.16f, dY + 0.06f, dZ + 0.2f}, {0.08f, 0.02f, 0.08f}, cHoloBlue * (std::sin(time * 3.0f + k) > 0.0f ? 1.0f : 0.2f));
        cube({dX - 2.0f + k*0.16f, dY + 0.06f, dZ + 0.0f}, {0.08f, 0.02f, 0.08f}, cHoloPurple * (std::cos(time * 2.0f + k) > 0.0f ? 1.0f : 0.2f));
    }
    cube({dX + 1.6f, dY + 0.05f, dZ + 0.1f}, {1.2f, 0.02f, 0.5f}, cConsole * 0.5f);
    for (int k = 0; k < 6; ++k) {
        cube({dX + 1.2f + k*0.16f, dY + 0.06f, dZ + 0.2f}, {0.08f, 0.02f, 0.08f}, cHoloPurple * (std::sin(time * 2.5f - k) > 0.0f ? 1.0f : 0.2f));
        cube({dX + 1.2f + k*0.16f, dY + 0.06f, dZ + 0.0f}, {0.08f, 0.02f, 0.08f}, cHoloBlue * (std::cos(time * 3.5f - k) > 0.0f ? 1.0f : 0.2f));
    }

    // ====================================================
    //  7. TRIPLE MONITOR CURVED COMMAND SETUP
    // ====================================================
    const float scrY = 0.55f;
    const float scrZ = -3.40f;
    const float sW = 2.45f, sH = 1.50f;
    const float bThick = 0.05f; 
    const float bDepth = 0.04f;

    // --- A. CENTRAL MONITOR (WEB CAM FEED) ---
    glm::mat4 sm = glm::translate(glm::mat4(1.0f), {0.0f, scrY, scrZ});
    sm = glm::rotate(sm, glm::radians(-12.0f), {1.0f, 0.0f, 0.0f});

    renderCube(glm::scale(glm::translate(sm, {0.0f, sH*0.5f + bThick*0.5f, 0.0f}), {sW + bThick*2.0f, bThick, bDepth}), view, projection, cHoloBlue);
    renderCube(glm::scale(glm::translate(sm, {0.0f, -sH*0.5f - bThick*0.5f, 0.0f}), {sW + bThick*2.0f, bThick, bDepth}), view, projection, cHoloBlue);
    renderCube(glm::scale(glm::translate(sm, {-sW*0.5f - bThick*0.5f, 0.0f, 0.0f}), {bThick, sH, bDepth}), view, projection, cHoloBlue);
    renderCube(glm::scale(glm::translate(sm, {sW*0.5f + bThick*0.5f, 0.0f, 0.0f}), {bThick, sH, bDepth}), view, projection, cHoloBlue);

    {
        glm::mat4 screenM = glm::translate(sm, {0.0f, 0.0f, 0.015f});
        screenM = glm::scale(screenM, {sW * 0.5f, sH * 0.5f, 1.0f});
        renderTexturedQuad(screenM, view, projection);
    }

    // --- B. LEFT MONITOR (DIAGNOSTIC SYSTEM MATRIX) ---
    glm::mat4 smLeft = glm::translate(glm::mat4(1.0f), {-sW - 0.25f, scrY, scrZ + 0.30f});
    smLeft = glm::rotate(smLeft, glm::radians(28.0f), {0.0f, 1.0f, 0.0f});
    smLeft = glm::rotate(smLeft, glm::radians(-12.0f), {1.0f, 0.0f, 0.0f});

    renderCube(glm::scale(glm::translate(smLeft, {0.0f, sH*0.5f + bThick*0.5f, 0.0f}), {sW + bThick*2.0f, bThick, bDepth}), view, projection, cHoloPurple);
    renderCube(glm::scale(glm::translate(smLeft, {0.0f, -sH*0.5f - bThick*0.5f, 0.0f}), {sW + bThick*2.0f, bThick, bDepth}), view, projection, cHoloPurple);
    renderCube(glm::scale(glm::translate(smLeft, {-sW*0.5f - bThick*0.5f, 0.0f, 0.0f}), {bThick, sH, bDepth}), view, projection, cHoloPurple);
    renderCube(glm::scale(glm::translate(smLeft, {sW*0.5f + bThick*0.5f, 0.0f, 0.0f}), {bThick, sH, bDepth}), view, projection, cHoloPurple);

    renderCube(glm::scale(glm::translate(smLeft, {0.0f, 0.0f, -0.01f}), {sW, sH, 0.01f}), view, projection, cWall * 1.5f);

    glm::mat4 lM = glm::translate(smLeft, {0.0f, 0.0f, 0.02f});
    glm::mat4 mvpL = projection * view * lM;
    drawOrbit3D(mvpL, 0.50f, cHoloPurple * 0.8f, 24, 1.5f);
    drawOrbit3D(mvpL, 0.30f, cHoloBlue * 0.8f, 16, 1.5f);
    drawOrbit3D(mvpL, std::fmod(time * 0.3f, 0.55f), cHoloBlue, 24, 1.0f);

    // --- C. RIGHT MONITOR (DYNAMIC EQUALIZER VISUALIZER) ---
    glm::mat4 smRight = glm::translate(glm::mat4(1.0f), {sW + 0.25f, scrY, scrZ + 0.30f});
    smRight = glm::rotate(smRight, glm::radians(-28.0f), {0.0f, 1.0f, 0.0f});
    smRight = glm::rotate(smRight, glm::radians(-12.0f), {1.0f, 0.0f, 0.0f});

    renderCube(glm::scale(glm::translate(smRight, {0.0f, sH*0.5f + bThick*0.5f, 0.0f}), {sW + bThick*2.0f, bThick, bDepth}), view, projection, cHoloBlue);
    renderCube(glm::scale(glm::translate(smRight, {0.0f, -sH*0.5f - bThick*0.5f, 0.0f}), {sW + bThick*2.0f, bThick, bDepth}), view, projection, cHoloBlue);
    renderCube(glm::scale(glm::translate(smRight, {-sW*0.5f - bThick*0.5f, 0.0f, 0.0f}), {bThick, sH, bDepth}), view, projection, cHoloBlue);
    renderCube(glm::scale(glm::translate(smRight, {sW*0.5f + bThick*0.5f, 0.0f, 0.0f}), {bThick, sH, bDepth}), view, projection, cHoloBlue);

    renderCube(glm::scale(glm::translate(smRight, {0.0f, 0.0f, -0.01f}), {sW, sH, 0.01f}), view, projection, cWall * 1.5f);

    for (int b = 0; b < 6; ++b) {
        float barH = 0.2f + 0.5f * std::sin(time * 6.0f + b * 1.5f) + 0.4f * std::cos(time * 3.0f - b);
        barH = glm::clamp(barH, 0.1f, sH * 0.85f);
        
        float bx = -sW * 0.4f + b * (sW * 0.16f);
        float by = -sH * 0.45f + barH * 0.5f;

        glm::mat4 barM = glm::translate(smRight, {bx, by, 0.02f});
        barM = glm::scale(barM, {0.20f, barH, 0.01f});
        renderCube(barM, view, projection, (b % 2 == 0) ? cHoloPurple : cHoloBlue * 1.2f);
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
    const glm::vec3 cFireCyan  {0.00f, 0.80f, 1.00f}; // Sci-fi cyan flame
    const glm::vec3 cTent      {0.75f, 0.65f, 0.50f}; // Bedouin tent canvas
    const glm::vec3 cDarkInside{0.15f, 0.12f, 0.10f}; // Tent inner darkness

    // ====================================================
    //  1. ENVIRONMENT (Floor & Sun)
    // ====================================================
    cube({0.0f, -1.15f, 0.0f}, {250.f, 0.08f, 250.f}, cSand); // Floor plane
    
    // Golden sun high on horizon (pulsates slightly)
    float sunPulse = 1.0f + 0.03f * std::sin(time * 1.5f);
    cube({0.0f, 4.20f, -22.0f}, {4.50f * sunPulse, 4.50f * sunPulse, 0.05f}, cSun);

    // ====================================================
    //  2. 360-DEGREE TWINKLING STARS
    // ====================================================
    for (int i = 0; i < 85; ++i) {
        float angle = float(i) * 0.147f;
        float r = 15.0f + std::sin(float(i) * 8.3f) * 3.0f;
        float x = r * std::cos(angle);
        float z = r * std::sin(angle);
        float y = 3.5f + std::abs(std::cos(float(i) * 15.7f)) * 6.0f;
        float twinkle = 0.3f + 0.7f * std::sin(time * 3.8f + float(i));
        cube({x, y, z}, {0.06f, 0.06f, 0.06f}, glm::vec3(0.9f, 0.95f, 1.0f) * twinkle);
    }

    // ====================================================
    //  3. THREE INTERCONNECTED OASIS WATER POOLS (Lifting slightly to prevent Z-fighting)
    // ====================================================
    cube({ 0.0f, -1.09f, -2.5f}, {3.2f, 0.01f, 3.4f}, cWater);      // Central pool
    cube({-3.2f, -1.09f, -5.2f}, {2.0f, 0.01f, 2.0f}, cWater);      // Left secondary pool
    cube({-1.6f, -1.09f, -3.8f}, {1.4f, 0.01f, 1.4f}, cWater * 1.1f); // Connecting stream

    // ====================================================
    //  4. 360-DEGREE DRIFTING CLOUDS
    // ====================================================
    auto drawDriftingCloud = [&](float baseStartX, float y, float z, float speed, float sizeScale, bool sideWays=false) {
        float offset = std::fmod(time * speed, 32.0f);
        float val = baseStartX + offset;
        if (val > 16.0f) val = -16.0f + (val - 16.0f);

        if (!sideWays) {
            cube({val, y, z}, {2.40f * sizeScale, 0.45f * sizeScale, 0.60f * sizeScale}, cCloud);
            cube({val + 0.40f * sizeScale, y + 0.20f * sizeScale, z}, {1.30f * sizeScale, 0.40f * sizeScale, 0.50f * sizeScale}, cCloud * 1.10f);
        } else {
            cube({z, y, val}, {0.60f * sizeScale, 0.45f * sizeScale, 2.40f * sizeScale}, cCloud);
            cube({z, y + 0.20f * sizeScale, val + 0.40f * sizeScale}, {0.50f * sizeScale, 0.40f * sizeScale, 1.30f * sizeScale}, cCloud * 1.10f);
        }
    };
    drawDriftingCloud(-8.0f,  3.60f, -14.0f, 0.20f, 1.20f);
    drawDriftingCloud( 5.0f,  4.20f, -14.0f, 0.12f, 0.95f);
    drawDriftingCloud(-12.0f, 3.80f,  14.0f, -0.18f, 1.15f);
    drawDriftingCloud( 2.0f,  4.50f,  14.0f, -0.25f, 1.00f);
    drawDriftingCloud(-14.0f, 3.90f, -14.0f, 0.16f, 1.10f, true);
    drawDriftingCloud( 14.0f, 4.30f,  14.0f, -0.22f, 1.05f, true);

    // ====================================================
    //  5. GEOMETRIC LOW-POLY SAND DUNES (Mid-ground)
    // ====================================================
    cube({-3.8f, -0.90f, -6.5f}, {4.0f, 0.45f, 4.5f}, cDarkSand);
    cube({ 3.8f, -0.85f, -7.0f}, {4.5f, 0.55f, 4.0f}, cDarkSand);
    cube({ 0.0f, -0.95f, -12.0f}, {10.0f, 0.58f, 3.8f}, cSand);
    cube({-10.0f, -0.80f, -1.0f}, {7.0f, 0.50f, 8.0f}, cDarkSand);
    cube({ 10.0f, -0.80f, -1.0f}, {7.0f, 0.50f, 8.0f}, cDarkSand);
    cube({-4.5f, -0.85f,  6.5f}, {5.0f, 0.60f, 5.0f}, cDarkSand);
    cube({ 4.5f, -0.90f,  7.0f}, {5.5f, 0.50f, 5.5f}, cDarkSand);
    cube({ 0.0f, -0.95f,  11.5f}, {11.0f, 0.65f, 4.0f}, cSand);

    // ====================================================
    //  6. PALM GROVES & ACACIA TREES (Core layout)
    // ====================================================
    auto drawAcacia = [&](glm::vec3 basePos, float scaleVal) {
        float trunkH = 1.50f * scaleVal;
        float trunkW = 0.12f * scaleVal;
        cube({basePos.x, basePos.y + trunkH*0.5f, basePos.z}, {trunkW, trunkH, trunkW}, cWood);
        cube({basePos.x, basePos.y + trunkH + 0.10f, basePos.z}, {1.40f*scaleVal, 0.18f*scaleVal, 1.40f*scaleVal}, cGreen);
        cube({basePos.x, basePos.y + trunkH + 0.28f, basePos.z}, {0.90f*scaleVal, 0.12f*scaleVal, 0.90f*scaleVal}, cGreen * 1.15f);
    };

    drawAcacia({-3.2f, -1.11f, -4.5f}, 1.10f);
    drawAcacia({ 3.0f, -1.11f, -4.2f}, 1.05f);
    drawAcacia({-5.2f, -1.11f, -7.0f}, 0.95f);
    drawAcacia({ 5.0f, -1.11f, -6.8f}, 0.90f);
    drawAcacia({-2.8f, -1.11f,  4.5f}, 1.08f);
    drawAcacia({ 3.2f, -1.11f,  4.8f}, 1.02f);
    drawAcacia({-4.8f, -1.11f,  6.5f}, 0.92f);
    drawAcacia({ 4.5f, -1.11f,  7.0f}, 0.96f);

    // ====================================================
    //  7. SAGUARO CACTI (Core layout)
    // ====================================================
    auto drawCactus = [&](float cx, float cz, float hVal) {
        cube({cx, -1.11f + hVal*0.5f, cz}, {0.09f, hVal, 0.09f}, cCactus);
        cube({cx - 0.12f, -1.11f + hVal*0.55f, cz}, {0.16f, 0.08f, 0.08f}, cCactus);
        cube({cx - 0.20f, -1.11f + hVal*0.72f, cz}, {0.08f, hVal*0.35f, 0.08f}, cCactus);
        cube({cx + 0.12f, -1.11f + hVal*0.42f, cz}, {0.16f, 0.08f, 0.08f}, cCactus);
        cube({cx + 0.20f, -1.11f + hVal*0.58f, cz}, {0.08f, hVal*0.32f, 0.08f}, cCactus);
    };
    drawCactus(-2.0f, -2.5f, 0.85f);
    drawCactus( 2.2f, -2.8f, 0.95f);
    drawCactus( 5.5f, -5.0f, 1.20f);
    drawCactus(-2.5f,  2.8f, 0.90f);
    drawCactus( 2.4f,  3.2f, 1.00f);
    drawCactus(-5.5f,  5.2f, 1.15f);
    drawCactus( 5.0f,  4.8f, 1.10f);

    // ====================================================
    //  8. BACKGROUND SCATTER (360 degrees, 12.0 to 30.0 units away)
    //  ONLY contains Acacia trees, Cacti and Clouds (no boxy pillars/blocks)
    // ====================================================
    for (int i = 0; i < 35; ++i) {
        float angle = float(i) * 0.18f;
        float dist = 12.0f + std::abs(std::sin(float(i) * 54.32f)) * 18.0f;
        float x = dist * std::cos(angle);
        float z = dist * std::sin(angle);

        // Avoid central oasis overlap
        if (std::abs(x) < 4.0f && std::abs(z) < 4.0f) continue;

        float typeHash = std::abs(std::sin(float(i) * 123.45f));
        float sc = 0.45f + (1.0f - (dist / 30.0f)) * 0.55f; // scale down with distance

        if (typeHash < 0.55f) {
            // Distant Acacia tree
            drawAcacia({x, -1.11f, z}, sc);
        } else {
            // Distant Cactus
            drawCactus(x, z, sc * 1.10f);
        }
    }

    // ====================================================
    //  9. RUINS & ANCIENT OBELISKS (Mid-ground)
    // ====================================================
    auto drawObelisk = [&](float obX, float obZ) {
        cube({obX, -0.90f, obZ}, {0.60f, 0.25f, 0.60f}, cStone);
        cube({obX, -0.05f, obZ}, {0.35f, 1.50f, 0.35f}, cStone * 0.9f);
        cube({obX,  0.75f, obZ}, {0.20f, 0.20f, 0.20f}, cLightStone);
    };
    drawObelisk(4.2f, -5.0f);
    drawObelisk(-4.2f, 5.0f);

    cube({-3.2f, -1.06f, -1.8f}, {1.40f, 0.26f, 0.26f}, cStone);       // Front Left broken column
    cube({ 3.5f, -1.06f,  4.0f}, {0.26f, 0.26f, 1.40f}, cStone * 0.95f); // Behind Right broken column

    // Weathered Rock Clusters around water pools
    cube({ 1.30f, -1.08f, -2.3f}, {0.24f, 0.16f, 0.22f}, cStone);
    cube({ 1.45f, -1.08f, -2.0f}, {0.18f, 0.10f, 0.18f}, cLightStone);
    cube({-1.35f, -1.08f, -2.1f}, {0.30f, 0.14f, 0.20f}, cStone);
    cube({-1.45f, -1.08f, -1.6f}, {0.16f, 0.08f, 0.16f}, cLightStone);

    // ====================================================
    //  10. BEDOUIN TENT
    // ====================================================
    const float tentX = -2.5f, tentZ = -1.2f;
    cube({tentX, -0.65f, tentZ}, {1.30f, 0.90f, 1.50f}, cTent);
    cube({tentX, -0.75f, tentZ + 0.74f}, {0.90f, 0.70f, 0.04f}, cDarkInside);
    cube({tentX - 0.60f, -0.65f, tentZ + 0.72f}, {0.05f, 0.90f, 0.05f}, cWood);
    cube({tentX + 0.60f, -0.65f, tentZ + 0.72f}, {0.05f, 0.90f, 0.05f}, cWood);

    // ====================================================
    //  11. CAMPFIRES & GLOWING ACCENTS
    // ====================================================
    const float fireX =  1.00f;
    const float fireZ = -1.20f;
    cube({fireX - 0.05f, -1.11f, fireZ}, {0.30f, 0.05f, 0.05f}, cWood);
    cube({fireX + 0.05f, -1.11f, fireZ}, {0.05f, 0.05f, 0.30f}, cWood);
    float fTime = time * 7.5f;
    float flameH = 0.12f + 0.06f * std::sin(fTime);
    cube({fireX, -1.05f + flameH*0.5f, fireZ}, {0.10f, flameH, 0.10f}, cFireOrange);
    cube({fireX, -1.02f + flameH*0.7f, fireZ}, {0.06f, flameH*0.6f, 0.06f}, cFireRed);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    cube({fireX, -0.95f, fireZ}, {0.35f, 0.40f, 0.35f}, glm::vec3(0.25f, 0.10f, 0.01f));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // South side Beacon
    const float bX = -0.50f;
    const float bZ =  6.20f;
    cube({bX, -0.80f, bZ}, {0.40f, 0.70f, 0.40f}, cStone);
    float bFlameH = 0.10f + 0.05f * std::sin(fTime * 0.8f);
    cube({bX, -0.40f + bFlameH*0.5f, bZ}, {0.08f, bFlameH, 0.08f}, cFireCyan);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    cube({bX, -0.32f, bZ}, {0.30f, 0.35f, 0.30f}, glm::vec3(0.01f, 0.18f, 0.25f));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // ====================================================
    //  12. FIREFLIES / PARTICLES
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
