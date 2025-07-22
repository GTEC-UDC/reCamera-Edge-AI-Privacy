#include "yoloscpp_detector.h"

// Hack to access private members of YOLOv11SegDetector
#define private public
#include "../external/YOLOs-CPP/include/seg/YOLO11Seg.hpp"
#undef private

#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>

YolosCppDetector::YolosCppDetector(const DetectorFactory::Parameters& params)
    : mParams(params), mPersonClassNumber(0) {
    try {
        std::cout << "Loading YOLO model from: " << mParams.modelPath << std::endl;
        std::cout << "Using GPU: " << mParams.useGPU << std::endl;
        mSegmentor = std::make_unique<YOLOv11SegDetector>(mParams.modelPath, mParams.labelsPath, mParams.useGPU);
        
        // Find the person class number in the class names
        const auto& classNames = mSegmentor->getClassNames();
        auto it = std::find(classNames.begin(), classNames.end(), "person");
        if (it != classNames.end()) {
            mPersonClassNumber = static_cast<int>(std::distance(classNames.begin(), it));
            std::cout << "Person class found at index: " << mPersonClassNumber << std::endl;
        } else {
            std::cerr << "Warning: could not find 'person' class in the labels. Using class ID 0." << std::endl;
            mPersonClassNumber = 0;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading YOLO model: " << e.what() << std::endl;
        throw;
    }
}

YolosCppDetector::~YolosCppDetector() = default;

bool YolosCppDetector::initialize() {
    return true;
}

bool YolosCppDetector::detect(const cv::Mat& frame, std::vector<Detection>& results) {
    if (frame.empty() || !mSegmentor) {
        return false;
    }
    
    try {
        // Use YOLOv11SegDetector to detect and segment humans
        float confThreshold = mParams.confidenceThreshold; 
        float iouThreshold = mParams.iouThreshold;
        
        // Perform segmentation using YOLOs-CPP
        auto segmentations = mSegmentor->segment(frame, confThreshold, iouThreshold);
        
        // Convert Segmentation results to our Detection format
        for (const auto& seg : segmentations) {
            Detection result;
            result.bbox = cv::Rect(seg.box.x, seg.box.y, seg.box.width, seg.box.height);
            result.confidence = seg.conf;
            result.classId = seg.classId;
            result.mask = seg.mask.clone(); // Clone the mask to ensure we own the data
            
            results.push_back(result);
        }

        if (mParams.debugMode) {
            cv::Mat debugImage = frame.clone();
            mSegmentor->drawSegmentationsAndBoxes(debugImage, segmentations);
            // Show the detections
            cv::imshow("Detections", debugImage);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error in human detection: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

// TODO: this is a hack to access the input size of the YOLOv11SegDetector
cv::Size YolosCppDetector::getInputSize() const {
    //return mSegmentor->getInputSize();
    return mSegmentor->inputImageShape;
}

int YolosCppDetector::getPersonClassId() const {
    return mPersonClassNumber;
}

const std::vector<std::string> &YolosCppDetector::getClassNames() const {
    return mSegmentor->getClassNames();
}
