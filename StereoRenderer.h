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
    // AR Mode 2 – Mini Solar System + scan laser + PnP corner reticles
    // ---------------------------------------------------------------

    // Renders an animated mini solar system (star + 3 orbital rings + planets)
    // floating over the detected ArUco marker.
    void renderSolarSystem(const glm::mat4& arModel, const glm::mat4& arProj, float time);

    // Sweeping laser scan line over the marker bounding box.
    void renderScanLaser(const std::vector<cv::Point2f>& corners2D,
                         int frameW, int frameH, float time);

    // Coloured crosshair reticles at the 4 ArUco corner positions.
    void renderCornerReticles(const std::vector<cv::Point2f>& corners2D,
                              int frameW, int frameH);

    // ---------------------------------------------------------------
    // AV Mode 3 – Zen Temple / Floating Art Gallery
    // The camera feed is displayed as a painting inside a golden frame.
    // ---------------------------------------------------------------
    void renderZenGallery(const glm::mat4& view, const glm::mat4& projection, float time);

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

    // ---- 3-D orbit ring helper (solar system) ----
    // Draws a circle of `segs` line segments in the XZ plane, transformed by mvp.
    void drawOrbit3D(const glm::mat4& mvp, float radius, glm::vec3 color,
                     int segs = 64, float lineWidth = 2.0f);

    // ---- RGB cube helper (kept for legacy renderGamerSetup) ----
    void renderRGB(const glm::mat4& model, const glm::mat4& view,
                   const glm::mat4& projection, float time, float hueOffset);

    // ---- shader programs ----
    GLuint quadProgram;      // background quad  (video texture, flat)
    GLuint objectProgram;    // solid-color 3D objects
    GLuint holoProgram;      // hologram (kept, not used in Mode 2 anymore)
    GLuint overlayProgram;   // 2-D screen-space lines
    GLuint rgbProgram;       // HSV-cycling RGB objects

    // ---- VAO / VBO ----
    GLuint quadVAO,    quadVBO;
    GLuint cubeVAO,    cubeVBO;
    GLuint overlayVAO, overlayVBO;  // 2-D flat lines (scan laser / reticles)
    GLuint ring3dVAO,  ring3dVBO;   // 3-D orbit ring lines (solar system)

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
