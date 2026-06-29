# Milgram Sandbox: Simulador del Continuo de Realidad-Virtualidad

Este proyecto es una aplicación interactiva desarrollada en **C++**, utilizando **OpenGL Moderno** (a través de las librerías GLFW y GLEW) y **OpenCV** para la visión artificial. El objetivo principal es plasmar de manera práctica todos los conceptos teóricos y matemáticos planteados en el informe de Computación Visual sobre Realidad Virtual (RV) y Realidad Aumentada (RA).

La aplicación demuestra de forma interactiva el **Continuo de Realidad-Virtualidad de Milgram**, permitiendo transitar dinámicamente entre el mundo físico y los entornos puramente virtuales.

---

## 🌌 Los 4 Modos del Continuo de Milgram

El núcleo del proyecto consiste en un bucle gráfico interactivo que permite al usuario alternar entre cuatro modos de visualización utilizando las teclas `1`, `2`, `3` y `4`:

```
[Entorno Real] ──(1)──> [Realidad Aumentada] ──(2)──> [Virtualidad Aumentada] ──(3)──> [Realidad Virtual]
```

### 1. Entorno Real (Tecla `1`)
*   **Qué hace:** Captura el feed de video en vivo de la cámara web (webcam) y lo dibuja en pantalla completa en la ventana de OpenGL como una textura en tiempo real.
*   **Objetivo:** Mostrar el punto de partida del continuo de Milgram (realidad pura sin aditivos digitales).

### 2. Realidad Aumentada - RA (Tecla `2`)
*   **Qué hace:** Utiliza **OpenCV** (módulo ArUco) para buscar un marcador impreso (ID 0) en el feed de la cámara. Al detectarlo, estima su orientación y posición 3D (pose) y dibuja un cubo 3D animado directamente sobre el marcador.
*   **Concepto clave:** Superponer información digital sobre el mundo real (Registro de cámara y tracking, Sección 3.2).

### 3. Virtualidad Aumentada - VA (Tecla `3`)
*   **Qué hace:** Renderiza un escenario virtual en 3D (un cuarto con una cuadrícula) pero integra un elemento real: una pantalla de "televisión" virtual que proyecta el feed en vivo de la cámara del usuario en tiempo real.
*   **Concepto clave:** Incorporar elementos del mundo real dentro de un entorno predominantemente virtual.

### 4. Realidad Virtual - RV (Tecla `4`)
*   **Qué hace:** Sumerge al usuario en un mundo 3D completamente digital. Utiliza **Renderizado Estereoscópico** (visión binocular, Sección 3.1) dividiendo la pantalla en dos vistas (ojo izquierdo y derecho) con una pequeña desviación de paralaje.
*   **Concepto clave:** Inmersión digital total. El usuario puede rotar y navegar la cámara libremente utilizando rotaciones matemáticas basadas en **Cuaterniones** para evitar el *Gimbal Lock* (Sección 3.3).

---

## 🛠️ Algoritmos y Matemáticas Utilizadas

1.  **Estimación de Pose 3D (Tracking):**
    A través de la detección de las 4 esquinas del marcador ArUco en 2D, resolvemos el problema de *Perspectiva desde N Puntos* (PnP) con `cv::solvePnP`. Esto nos genera un vector de rotación ($\vec{r}$) y traslación ($\vec{t}$).
2.  **Transformación OpenCV a OpenGL:**
    Convertimos la rotación de OpenCV (basada en el teorema de Rodrigues) a una matriz homogénea de $4 \times 4$. Reorientamos los ejes de la cámara (ya que en OpenCV el eje Y apunta hacia abajo y Z hacia el frente, mientras que en OpenGL Y apunta hacia arriba y Z hacia atrás) para alinear ambos mundos.
3.  **Renderizado Estereoscópico:**
    Configuramos dos viewports independientes mediante `glViewport` en la misma ventana. Calculamos una matriz de vista modificada para cada ojo:
    $$\text{View}_{\text{Ojo}} = \text{View}_{\text{Base}} \pm \text{Translación}(\text{IPD}/2, 0, 0)$$
    donde $\text{IPD}$ es la distancia interpupilar para dar la ilusión de profundidad en 3D.
4.  **Cuaterniones para Rotación de Cámara:**
    Para evitar las limitaciones de los ángulos de Euler, la cámara virtual realiza sus interpolaciones de giro utilizando cuaterniones ($q = w + xi + yj + zk$), asegurando rotaciones suaves e ilimitadas.

---

## 📂 Estructura del Código

*   **[CMakeLists.txt](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/CMakeLists.txt):** Archivo de configuración para automatizar la compilación del proyecto y vincular las librerías necesarias.
*   **[MathUtils.h](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/MathUtils.h):** Utilidades matemáticas para la conversión de matrices OpenCV a matrices de OpenGL y el cálculo de interpolaciones SLERP con cuaterniones.
*   **[MarkerTracker.h](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/MarkerTracker.h) / [MarkerTracker.cpp](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/MarkerTracker.cpp):** Captura de cámara con OpenCV y lógica de seguimiento de marcadores ArUco.
*   **[ContinuumManager.h](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/ContinuumManager.h):** Máquina de estados que gestiona la transición entre los 4 modos del continuo de Milgram.
*   **[StereoRenderer.h](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/StereoRenderer.h) / [StereoRenderer.cpp](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/StereoRenderer.cpp):** Motor gráfico en OpenGL encargado de compilar shaders, renderizar la cámara de fondo, dibujar los cubos 3D y procesar la pantalla dividida estereoscópica.
*   **[main.cpp](file:///c:/Users/rodri/OneDrive/Desktop/Proyectos/cv/main.cpp):** Archivo principal que integra todos los módulos y contiene el loop de renderizado gráfico de GLFW.

---

## ⚙️ Requisitos para Compilar

El proyecto está diseñado para compilarse en cualquier sistema operativo usando **CMake** (versión 3.15+) y un compilador de C++ compatible con **C++17** (como GCC de MSYS2 en Windows).

Dependencias necesarias:
*   **OpenCV 4.x** (con soporte para el módulo ArUco)
*   **GLFW3** (gestión de ventanas y contexto OpenGL)
*   **GLEW** (cargador de extensiones OpenGL)
*   **GLM** (librería matemática de OpenGL)
