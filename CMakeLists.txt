cmake_minimum_required(VERSION 3.15)
project(MilgramSandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(OpenCV REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

# Executable targets
add_executable(MilgramSandbox
    main.cpp
    MarkerTracker.cpp
    StereoRenderer.cpp
)

# Include paths
target_include_directories(MilgramSandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(MilgramSandbox PRIVATE
    glfw
    GLEW::GLEW
    ${OpenCV_LIBS}
    OpenGL::GL
)
