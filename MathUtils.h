#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

namespace MathUtils {
    // Converts OpenCV rotation vector (rvec) and translation vector (tvec) 
    // to a 4x4 OpenGL/GLM model-view matrix.
    inline glm::mat4 openCVPoseToGLM(const cv::Mat& rvec, const cv::Mat& tvec) {
        cv::Mat R;
        cv::Rodrigues(rvec, R); // Convert rotation vector to 3x3 rotation matrix
        
        glm::mat4 modelView = glm::mat4(1.0f);
        
        // Copy rotation (transposing because OpenCV is row-major and GLM is column-major)
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                modelView[i][j] = static_cast<float>(R.at<double>(j, i));
            }
        }
        
        // Copy translation
        modelView[3][0] = static_cast<float>(tvec.at<double>(0));
        modelView[3][1] = static_cast<float>(tvec.at<double>(1));
        modelView[3][2] = static_cast<float>(tvec.at<double>(2));
        modelView[3][3] = 1.0f;

        // In OpenCV: Y points down, Z points forward.
        // In OpenGL: Y points up, Z points backward.
        // We invert Y and Z camera coordinate axes to map to OpenGL space.
        glm::mat4 cvToGl = glm::mat4(1.0f);
        cvToGl[1][1] = -1.0f; // Invert Y
        cvToGl[2][2] = -1.0f; // Invert Z
        
        return cvToGl * modelView;
    }

    // Interpolates between two camera rotations smoothly using SLERP (quaternions)
    inline glm::quat interpolateRotations(const glm::quat& q1, const glm::quat& q2, float factor) {
        return glm::slerp(q1, q2, factor);
    }
}
