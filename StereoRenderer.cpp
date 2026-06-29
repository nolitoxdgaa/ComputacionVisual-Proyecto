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
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
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

StereoRenderer::StereoRenderer() 
    : quadProgram(0), objectProgram(0), quadVAO(0), quadVBO(0), 
      cubeVAO(0), cubeVBO(0), videoTextureId(0), 
      quadTexLocation(-1), objMVPLocation(-1), objColorLocation(-1) {}

StereoRenderer::~StereoRenderer() {
    if (quadProgram) glDeleteProgram(quadProgram);
    if (objectProgram) glDeleteProgram(objectProgram);
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO) glDeleteBuffers(1, &quadVBO);
    if (cubeVAO) glDeleteVertexArrays(1, &cubeVAO);
    if (cubeVBO) glDeleteBuffers(1, &cubeVBO);
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

    if (!quadProgram || !objectProgram) {
        std::cerr << "Failed to compile/link shader programs!" << std::endl;
        return false;
    }

    // Get uniform locations
    quadTexLocation = glGetUniformLocation(quadProgram, "videoTexture");
    objMVPLocation = glGetUniformLocation(objectProgram, "uMVP");
    objColorLocation = glGetUniformLocation(objectProgram, "uColor");

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

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
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
