# Guía de Configuración e Instalación del Entorno

Este proyecto utiliza **C++**, **CMake**, **OpenGL** (GLFW + GLEW + GLM) y **OpenCV 4**. Para facilitar el trabajo colaborativo, puedes automatizar la instalación de todas las dependencias utilizando tu asistente **Antigravity**.

---

## ⚡ Instalación Automática con Antigravity (Recomendada)

Si estás utilizando el asistente virtual **Antigravity**, solo debes copiar el siguiente prompt y pegarlo directamente en el chat del agente. Este se encargará de configurar tu compilador, instalar las librerías necesarias y dejar listo tu entorno en VS Code.

> [!TIP]
> **Copia y pega el siguiente texto en el chat de tu Antigravity:**

```text
Hola Antigravity. Estoy colaborando en un proyecto de Computación Visual en C++ con OpenGL y OpenCV en Windows. Por favor, configura mi entorno de desarrollo en mi máquina realizando los siguientes pasos:

1. Verifica si tengo MSYS2 instalado en la ruta por defecto "C:\msys64".
2. Actualiza la base de datos de paquetes y todo el sistema de pacman ejecutando "C:\msys64\usr\bin\pacman.exe -Syu --noconfirm". (Nota: si se cuelga en el paso 'checking available disk space', edita "C:\msys64\etc\pacman.conf" para comentar la línea "CheckSpace" poniéndole un "#", fuerza la eliminación del archivo de bloqueo "C:\msys64\var\lib\pacman\db.lck" si existe, y reintenta la actualización).
3. Instala los siguientes paquetes de desarrollo en el entorno MINGW64 de mi MSYS2:
   - CMake: mingw-w64-x86_64-cmake
   - OpenCV: mingw-w64-x86_64-opencv
   - FreeGLUT: mingw-w64-x86_64-freeglut
   - GLFW: mingw-w64-x86_64-glfw
   - GLEW: mingw-w64-x86_64-glew
   - GLM: mingw-w64-x86_64-glm
4. Asegúrate de que las rutas de los binarios del compilador y de cmake estén listas para usarse.
5. Genera o verifica que los archivos de configuración ".vscode/c_cpp_properties.json" y ".vscode/settings.json" incluyan las rutas de inclusión de OpenCV ("C:/msys64/mingw64/include/opencv4") y las librerías de MSYS2 para que el editor no muestre líneas rojas de error en los #include.
```

---

## 🛠️ Instalación Manual (Paso a Paso)

Si prefieres realizar la instalación tú mismo desde la terminal, sigue estos pasos:

### 1. Descargar e Instalar MSYS2
1. Descarga el instalador desde [msys2.org](https://www.msys2.org/) e instálalo en la ruta por defecto: `C:\msys64`.

### 2. Configurar Pacman (Evitar cuelgues)
Para prevenir que la instalación se quede congelada en Windows al comprobar espacio de disco:
1. Abre el archivo `C:\msys64\etc\pacman.conf` en un editor de texto.
2. Busca la línea `CheckSpace` y coméntala colocándole un `#` al inicio:
   ```text
   #CheckSpace
   ```

### 3. Ejecutar la Instalación de Librerías
Abre la consola de **MSYS2 MinGW 64-bit** (búscala en el menú inicio de Windows) y ejecuta los siguientes comandos para actualizar e instalar todas las dependencias:

```bash
# Actualizar base de datos de paquetes y sistema
pacman -Syu --noconfirm

# Instalar herramientas de compilación, OpenCV, OpenGL y utilidades
pacman -S --noconfirm mingw-w64-x86_64-cmake \
                      mingw-w64-x86_64-opencv \
                      mingw-w64-x86_64-freeglut \
                      mingw-w64-x86_64-glfw \
                      mingw-w64-x86_64-glew \
                      mingw-w64-x86_64-glm
```

### 4. Configurar Variables de Entorno en Windows
Asegúrate de agregar `C:\msys64\mingw64\bin` a la variable de entorno `PATH` de tu sistema operativo Windows para que tu terminal reconozca los comandos `g++`, `gcc` y `cmake` de forma global.

---

## 🚀 Compilación y Ejecución del Proyecto

Una vez que tengas configuradas las librerías, abre una terminal de PowerShell en la carpeta raíz del proyecto y ejecuta:

```powershell
# Crear y entrar a la carpeta de compilación
mkdir build
cd build

# Configurar el proyecto con CMake utilizando Ninja (instalado con CMake en MSYS2)
cmake -G "Ninja" -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 ..

# Compilar
cmake --build .

# Ejecutar el simulador
.\MilgramSandbox.exe
```
