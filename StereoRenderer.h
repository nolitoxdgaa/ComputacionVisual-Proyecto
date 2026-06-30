#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <opencv2/opencv.hpp>
#include <functional>
#include <string>
#include <vector>

class StereoRenderer {
public:
    StereoRenderer();
    ~StereoRenderer();

    // Initializes OpenGL buffers, VAOs, VBOs, and compiles shaders.
    bool initialize();

    // Updates the background video texture with the latest OpenCV frame.
    void updateVideoTexture(const cv::Mat& frame);

    // Renders the background video feed (2D fullscreen quad, identity MVP).
    void renderBackground();

    // Renders a textured 3D quad using the video texture (for virtual TV screens).
    void renderTexturedQuad(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);

    // Renders a simple 3D solid-color cube with the given MVP matrices.
    void renderCube(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color);

    // ---------------------------------------------------------------
    // AR Mode 2 – hologram + scan laser + PnP corner reticles
    // ---------------------------------------------------------------
    void renderHologram(const glm::mat4& arModel, const glm::mat4& arProj, float time);

    void renderScanLaser(const std::vector<cv::Point2f>& corners2D,
                         int frameW, int frameH, float time);

    void renderCornerReticles(const std::vector<cv::Point2f>& corners2D,
                              int frameW, int frameH);

    // ---------------------------------------------------------------
    // AV Mode 3 – RGB Gamer Setup scene
    // time: glfwGetTime(), used to drive the RGB color cycling and animations.
    // The monitor screen uses the video texture already uploaded via updateVideoTexture().
    // ---------------------------------------------------------------
    void renderGamerSetup(const glm::mat4& view, const glm::mat4& projection, float time);

    // ---------------------------------------------------------------
    // VR Mode 4 – basic virtual scene (used inside stereo callback too)
    // ---------------------------------------------------------------
    void renderVirtualScene(const glm::mat4& view, const glm::mat4& projection);

    // Renders the scene in stereoscopic split-screen (side-by-side).
    void renderStereo(int windowWidth, int windowHeight, float eyeSeparation,
                      const glm::mat4& baseView, const glm::mat4& projection,
                      const std::function<void(const glm::mat4&, const glm::mat4&)>& renderSceneCallback);

private:
    // ---- shader compile helpers ----
    GLuint compileShader(GLenum type, const std::string& source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    // ---- 2-D overlay helper (scan laser + reticles) ----
    void drawLines2D(const std::vector<float>& verts, const glm::vec4& color, float lineWidth = 2.0f);

    // ---- RGB cube helper (gamer scene) ----
    void renderRGB(const glm::mat4& model, const glm::mat4& view,
                   const glm::mat4& projection, float time, float hueOffset);

    // ---- shader programs ----
    GLuint quadProgram;      // background quad  (video texture, flat)
    GLuint objectProgram;    // solid-color 3D objects
    GLuint holoProgram;      // hologram (Mode 2)
    GLuint overlayProgram;   // 2-D screen-space lines
    GLuint rgbProgram;       // HSV-cycling RGB objects (Mode 3)

    // ---- VAO / VBO ----
    GLuint quadVAO,    quadVBO;
    GLuint cubeVAO,    cubeVBO;
    GLuint overlayVAO, overlayVBO;

    // ---- textures ----
    GLuint videoTextureId;

    // ---- uniform locations ----
    GLint quadTexLocation;
    GLint quadMVPLocation;

    GLint objMVPLocation;
    GLint objColorLocation;

    GLint holoMVPLocation;
    GLint holoTimeLocation;
    GLint holoColorLocation;

    GLint overlayColorLocation;

    GLint rgbMVPLocation;
    GLint rgbTimeLocation;
    GLint rgbHueOffsetLocation;
};
