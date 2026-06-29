#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <vector>

class MarkerTracker {
public:
    MarkerTracker();
    ~MarkerTracker();

    bool initializeCamera(int cameraIndex = 0);
    bool grabFrame();
    
    // Returns the current captured frame
    cv::Mat getFrame() const { return currentFrame; }
    
    // Detects markers and estimates the pose of the specified marker ID.
    // Returns true if the marker is detected, and fills out rvec and tvec.
    bool trackMarker(int targetId, cv::Mat& rvec, cv::Mat& tvec);

    // Camera matrix and distortion coefficients
    cv::Mat getCameraMatrix() const { return cameraMatrix; }
    cv::Mat getDistCoeffs() const { return distCoeffs; }

private:
    cv::VideoCapture cap;
    cv::Mat currentFrame;
    cv::Mat cameraMatrix;
    cv::Mat distCoeffs;
    float markerSize; // In meters

    cv::aruco::ArucoDetector detector;
};
