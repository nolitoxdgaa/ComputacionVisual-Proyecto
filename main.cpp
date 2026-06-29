#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "ContinuumManager.h"
#include "MarkerTracker.h"
#include "StereoRenderer.h"
#include "MathUtils.h"

// ==========================================
// CONFIGURACION GLOBAL
// ==========================================
const int WINDOW_WIDTH  = 1280;
const int WINDOW_HEIGHT = 720;
const char* WINDOW_TITLE = "Milgram Sandbox - Continuo de Realidad-Virtualidad";

// Distancia interpupilar para modo RV (en metros)
const float IPD = 0.065f;

// Angulos de camara virtual (Modo VA y RV)
float cameraYaw   = 0.0f;
float cameraPitch = 0.0f;
double lastMouseX = 0.0, lastMouseY = 0.0;
bool firstMouse = true;

// Managers globales (se inicializan en main)
ContinuumManager* g_continuum = nullptr;

// ==========================================
// CALLBACKS DE GLFW
// ==========================================

// Teclado: cambio entre modos del continuo
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (g_continuum && key >= GLFW_KEY_1 && key <= GLFW_KEY_4) {
            g_continuum->handleKeyPress('1' + (key - GLFW_KEY_1));
        }
    }
}

// Mouse: controlar la camara en modo VA y RV
void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastMouseX = xpos;
        lastMouseY = ypos;
        firstMouse = false;
    }
    float sensitivity = 0.15f;
    cameraYaw   += (float)(xpos - lastMouseX) * sensitivity;
    cameraPitch -= (float)(ypos - lastMouseY) * sensitivity;
    cameraPitch  = glm::clamp(cameraPitch, -89.0f, 89.0f);
    lastMouseX = xpos;
    lastMouseY = ypos;
}

// ==========================================
// FUNCION PRINCIPAL
// ==========================================
int main() {
    // --- 1. Inicializar GLFW ---
    if (!glfwInit()) {
        std::cerr << "[ERROR] No se pudo inicializar GLFW." << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    if (!window) {
        std::cerr << "[ERROR] No se pudo crear la ventana GLFW." << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // --- 2. Inicializar GLEW ---
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "[ERROR] No se pudo inicializar GLEW." << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    // --- 3. Inicializar modulos ---
    ContinuumManager continuum;
    g_continuum = &continuum;

    MarkerTracker tracker;
    bool cameraOk = tracker.initializeCamera(0);
    if (!cameraOk) {
        std::cerr << "[AVISO] No se pudo abrir la camara web. Los modos RA y VA quedan desactivados." << std::endl;
    }

    StereoRenderer renderer;
    if (!renderer.initialize()) {
        std::cerr << "[ERROR] Fallo al inicializar el StereoRenderer." << std::endl;
        return -1;
    }

    // --- 4. Matrices de proyeccion y vista ---
    glm::mat4 projection = glm::perspective(
        glm::radians(60.0f),
        (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT,
        0.01f, 100.0f
    );

    // Variables de pose para Realidad Aumentada
    cv::Mat rvec, tvec;
    bool markerDetected = false;

    // ==========================================
    // 5. LOOP PRINCIPAL
    // ==========================================
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Capturar frame de la camara
        if (cameraOk) {
            tracker.grabFrame();
        }

        // Limpiar pantalla
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

        // --- Construir la vista de la camara virtual con Yaw/Pitch (Cuaterniones internamente en GLM) ---
        glm::quat quatYaw   = glm::angleAxis(glm::radians(cameraYaw),   glm::vec3(0, 1, 0));
        glm::quat quatPitch = glm::angleAxis(glm::radians(cameraPitch), glm::vec3(1, 0, 0));
        glm::mat4 cameraRotation = glm::mat4_cast(glm::normalize(quatYaw * quatPitch));
        glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f));
        glm::mat4 virtualView = glm::inverse(cameraRotation * cameraTranslation);

        // ==========================================
        // MAQUINA DE ESTADOS: Continuo de Milgram
        // ==========================================
        switch (continuum.getState()) {

            // --------------------------------------------------
            // MODO 1: ENTORNO REAL
            // Muestra solo el feed de la camara web en pantalla.
            // --------------------------------------------------
            case ContinuumState::REAL_ENVIRONMENT:
                if (cameraOk) {
                    renderer.updateVideoTexture(tracker.getFrame());
                    renderer.renderBackground();
                }
                break;

            // --------------------------------------------------
            // MODO 2: REALIDAD AUMENTADA
            // Feed de camara + cubo 3D sobre marcador ArUco.
            // --------------------------------------------------
            case ContinuumState::AUGMENTED_REALITY:
                if (cameraOk) {
                    cv::Mat frame = tracker.getFrame();
                    renderer.updateVideoTexture(frame);
                    renderer.renderBackground();

                    // Intentar detectar marcador con ID 0
                    markerDetected = tracker.trackMarker(0, rvec, tvec);

                    if (markerDetected) {
                        // Convertir pose de OpenCV a OpenGL
                        glm::mat4 arModel = MathUtils::openCVPoseToGLM(rvec, tvec);
                        arModel = glm::scale(arModel, glm::vec3(0.08f));

                        // Construir matriz de proyeccion a partir de la camara calibrada
                        cv::Mat K = tracker.getCameraMatrix();
                        float fx = (float)K.at<double>(0, 0);
                        float fy = (float)K.at<double>(1, 1);
                        float cx = (float)K.at<double>(0, 2);
                        float cy = (float)K.at<double>(1, 2);
                        float near_plane = 0.01f, far_plane = 100.0f;

                        glm::mat4 arProj = glm::mat4(0.0f);
                        arProj[0][0] = 2.0f * fx / (float)frame.cols;
                        arProj[1][1] = 2.0f * fy / (float)frame.rows;
                        arProj[2][0] = 1.0f - 2.0f * cx / (float)frame.cols;
                        arProj[2][1] = 2.0f * cy / (float)frame.rows - 1.0f;
                        arProj[2][2] = -(far_plane + near_plane) / (far_plane - near_plane);
                        arProj[2][3] = -1.0f;
                        arProj[3][2] = -2.0f * far_plane * near_plane / (far_plane - near_plane);

                        renderer.renderCube(arModel, glm::mat4(1.0f), arProj, glm::vec3(0.2f, 0.7f, 1.0f));
                    }
                }
                break;

            // --------------------------------------------------
            // MODO 3: VIRTUALIDAD AUMENTADA
            // Entorno 3D virtual con feed de camara proyectado
            // en una pantalla dentro del mundo virtual.
            // --------------------------------------------------
            case ContinuumState::AUGMENTED_VIRTUALITY:
                renderer.renderVirtualScene(virtualView, projection);

                // Renderizar una "pantalla de TV" virtual con el feed de la camara
                if (cameraOk) {
                    renderer.updateVideoTexture(tracker.getFrame());

                    // Posicionar la pantalla TV frente al usuario en el mundo virtual
                    glm::mat4 tvModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, -3.0f));
                    tvModel = glm::scale(tvModel, glm::vec3(3.0f, 2.0f, 0.05f));

                    // TODO (Avanzado): Renderizar la textura del video en este quad 3D
                    // Por ahora se renderiza como un cubo blanco plano en posicion de pantalla
                    renderer.renderCube(tvModel, virtualView, projection, glm::vec3(0.9f, 0.9f, 0.9f));
                }
                break;

            // --------------------------------------------------
            // MODO 4: REALIDAD VIRTUAL
            // Entorno totalmente virtual con vista esteroscopica
            // (pantalla dividida side-by-side para lentes VR).
            // --------------------------------------------------
            case ContinuumState::VIRTUAL_REALITY:
                renderer.renderStereo(
                    WINDOW_WIDTH, WINDOW_HEIGHT, IPD,
                    virtualView, projection,
                    [&](const glm::mat4& eyeView, const glm::mat4& eyeProj) {
                        glClear(GL_DEPTH_BUFFER_BIT);
                        renderer.renderVirtualScene(eyeView, eyeProj);
                    }
                );
                break;
        }

        // Actualizar titulo con el estado actual
        std::string title = std::string(WINDOW_TITLE) + " | Modo: " + continuum.getStateName() + " | [1-4] cambiar | [Esc] salir";
        glfwSetWindowTitle(window, title.c_str());

        glfwSwapBuffers(window);
    }

    // --- 6. Limpieza ---
    g_continuum = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
