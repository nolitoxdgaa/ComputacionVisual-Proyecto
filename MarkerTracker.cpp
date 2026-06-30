#include "MarkerTracker.h"
#include <iostream>

MarkerTracker::MarkerTracker() : markerSize(0.1f) {
    // Set up default camera intrinsic matrix (based on a generic 640x480 camera)
    cameraMatrix = (cv::Mat_<double>(3, 3) << 
        600.0,   0.0, 320.0,
          0.0, 600.0, 240.0,
          0.0,   0.0,   1.0);
          
    // Set up default distortion coefficients (zero distortion)
    distCoeffs = (cv::Mat_<double>(1, 5) << 0, 0, 0, 0, 0);

    // Initialize ArUco detector (6x6 markers, dictionary DICT_6X6_250)
    cv::aruco::Dictionary dictionary = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::aruco::DetectorParameters detectorParams = cv::aruco::DetectorParameters();
    detector = cv::aruco::ArucoDetector(dictionary, detectorParams);
}

MarkerTracker::~MarkerTracker() {
    if (cap.isOpened()) {
        cap.release();
    }
}

bool MarkerTracker::initializeCamera(int cameraIndex) {
    cap.open(cameraIndex, cv::CAP_DSHOW); // CAP_DSHOW is faster on Windows
    if (!cap.isOpened()) {
        // Fallback to default API if DSHOW fails
        cap.open(cameraIndex);
    }
    
    if (cap.isOpened()) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        return true;
    }
    return false;
}

bool MarkerTracker::grabFrame() {
    if (!cap.isOpened()) return false;
    cap >> currentFrame;
    return !currentFrame.empty();
}

bool MarkerTracker::trackMarker(int targetId, cv::Mat& rvec, cv::Mat& tvec) {
    if (currentFrame.empty()) return false;

    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners;
    detector.detectMarkers(currentFrame, corners, ids);

    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == targetId) {
            // Define 3D coordinates of marker corners in its own local coordinate system
            std::vector<cv::Point3f> objectPoints = {
                cv::Point3f(-markerSize/2.f, markerSize/2.f, 0.f),
                cv::Point3f(markerSize/2.f, markerSize/2.f, 0.f),
                cv::Point3f(markerSize/2.f, -markerSize/2.f, 0.f),
                cv::Point3f(-markerSize/2.f, -markerSize/2.f, 0.f)
            };

            // Estimate pose using PnP
            lastDetectedCorners = corners[i];
            cv::solvePnP(objectPoints, corners[i], cameraMatrix, distCoeffs, rvec, tvec);
            return true;
        }
    }
    return false;
}
