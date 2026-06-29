#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <opencv2/opencv.hpp>
#include <functional>
#include <string>

class StereoRenderer {
public:
    StereoRenderer();
    ~StereoRenderer();

    // Initializes OpenGL buffers, VAOs, VBOs, and compiles shaders.
    bool initialize();

    // Updates the background video texture with the latest OpenCV frame.
    void updateVideoTexture(const cv::Mat& frame);

    // Renders the background video feed.
    void renderBackground();

    // Renders a textured 3D quad using the video texture (for virtual TV screens).
    void renderTexturedQuad(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);

    // Renders a simple 3D object (a cube) with the given model, view, and projection matrices.
    void renderCube(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color);

    // Renders the virtual environment (a simple grid/room).
    void renderVirtualScene(const glm::mat4& view, const glm::mat4& projection);

    // Renders the scene in stereoscopic split-screen (side-by-side).
    // eyeSeparation: distance between eyes (interpupillary offset).
    // renderSceneCallback: a function or lambda to render the 3D scene from the current eye's viewpoint.
    void renderStereo(int windowWidth, int windowHeight, float eyeSeparation,
                      const glm::mat4& baseView, const glm::mat4& projection,
                      const std::function<void(const glm::mat4&, const glm::mat4&)>& renderSceneCallback);

private:
    // Helper functions to compile and link shader programs
    GLuint compileShader(GLenum type, const std::string& source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    // Shader programs
    GLuint quadProgram;
    GLuint objectProgram;

    // Buffers and VAOs
    GLuint quadVAO, quadVBO;
    GLuint cubeVAO, cubeVBO;

    // Video texture ID
    GLuint videoTextureId;

    // Shader locations
    GLint quadTexLocation;
    GLint quadMVPLocation;
    GLint objMVPLocation;
    GLint objColorLocation;
};
