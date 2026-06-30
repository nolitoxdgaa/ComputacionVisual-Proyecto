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
    // AR Mode 2 – enhanced hologram rendering
    // ---------------------------------------------------------------

    // Renders the animated hologram pyramid/robot over the ArUco marker.
    // arModel   : OpenCV-to-OpenGL pose matrix (from MathUtils::openCVPoseToGLM)
    // arProj    : Projection built from the calibrated camera intrinsics
    // time      : glfwGetTime() – drives the sine-wave deformation and scanlines
    void renderHologram(const glm::mat4& arModel, const glm::mat4& arProj, float time);

    // Draws a horizontal laser scan line that sweeps up and down over the marker.
    // corners2D : the 4 pixel-space corners returned by MarkerTracker::getLastCorners()
    // frameW/H  : camera frame dimensions (640 x 480 by default)
    // time      : glfwGetTime()
    void renderScanLaser(const std::vector<cv::Point2f>& corners2D,
                         int frameW, int frameH, float time);

    // Draws small crosshair reticles at the 4 detected ArUco corners (PnP viz).
    void renderCornerReticles(const std::vector<cv::Point2f>& corners2D,
                              int frameW, int frameH);

    // ---------------------------------------------------------------

    // Renders the virtual environment (a simple grid/room).
    void renderVirtualScene(const glm::mat4& view, const glm::mat4& projection);

    // Renders the scene in stereoscopic split-screen (side-by-side).
    void renderStereo(int windowWidth, int windowHeight, float eyeSeparation,
                      const glm::mat4& baseView, const glm::mat4& projection,
                      const std::function<void(const glm::mat4&, const glm::mat4&)>& renderSceneCallback);

private:
    // Helper functions to compile and link shader programs
    GLuint compileShader(GLenum type, const std::string& source);
    GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    // Helper: build NDC lines VBO and draw them
    void drawLines2D(const std::vector<float>& verts, const glm::vec4& color, float lineWidth = 2.0f);

    // Shader programs
    GLuint quadProgram;
    GLuint objectProgram;
    GLuint holoProgram;    // hologram animated shader
    GLuint overlayProgram; // 2-D flat screen-space lines

    // Buffers and VAOs
    GLuint quadVAO, quadVBO;
    GLuint cubeVAO, cubeVBO;
    GLuint overlayVAO, overlayVBO; // dynamic 2D lines

    // Video texture ID
    GLuint videoTextureId;

    // Shader uniform locations
    GLint quadTexLocation;
    GLint quadMVPLocation;
    GLint objMVPLocation;
    GLint objColorLocation;
    GLint holoMVPLocation;
    GLint holoTimeLocation;
    GLint holoColorLocation;
    GLint overlayColorLocation;
};
