#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <memory>

#include "idetector.h"

class VideoAnonymizer {
public:
    struct Parameters {
        float confThreshold;
        float iouThreshold;
        std::string modelPath;
        int warmupFrames;
        float dilationFactor;
        int minDilation;
        int maxDilation;
        int dilationIterations;
        float warmupDilationFactor;
        float learningRate;
        std::string labelsPath;
        bool useGPU;
        bool debugMode;
        // Constructor with default values
        Parameters() :
            confThreshold(0.5f),
            iouThreshold(0.45f),
            modelPath("yolo11n-seg.onnx"),
            warmupFrames(30),
            dilationFactor(0.1f),
            minDilation(5),
            maxDilation(30),
            dilationIterations(1),
            warmupDilationFactor(0.15f),
            learningRate(0.2f),
            labelsPath("coco.names"),
            useGPU(false),
            debugMode(false) {}
    };

    VideoAnonymizer(const Parameters& params = Parameters());
    ~VideoAnonymizer();

    // Process a frame with optional debug mode
    cv::Mat processFrame(const cv::Mat& frame);
    
    // Get current background model
    cv::Mat getBackground() const;
    
    // Get the latest human detection mask
    cv::Mat getDetectionMask() const;
    
    // Get the latest detection results for visualization
    const std::vector<IDetector::Detection>& getDetections() const;
    
    // Reset the anonymizer state
    void reset();

private:
    Parameters mParams;
    std::unique_ptr<IDetector> mDetector;
    cv::Mat mBackground;
    cv::Mat mLastDetectionMask;
    int mFrameCount;
    std::vector<IDetector::Detection> mLastDetections;

    // Detect humans in the frame using HumanDetector
    void detectHumans(const cv::Mat& frame, cv::Mat& mask);
    
    // Update the background model with the current frame
    void updateBackground(const cv::Mat& frame, const cv::Mat& humanMask);
    
    // Create and apply dilated mask from detections
    cv::Mat createCombinedMask(const cv::Mat& frame, const cv::Mat& mask);
    
    // Apply anonymization by replacing human pixels with background
    cv::Mat applyMaskToFrame(const cv::Mat& frame, const cv::Mat& mask);

    // Create a mask from the detection results
    cv::Mat createMask(const cv::Mat& frame, const std::vector<IDetector::Detection>& detections);
};
