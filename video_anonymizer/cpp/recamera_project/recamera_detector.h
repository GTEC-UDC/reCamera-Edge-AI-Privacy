#ifndef RECAMERA_DETECTOR_H
#define RECAMERA_DETECTOR_H

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "../common/idetector.h"

/**
 * @brief RecameraDetector implements the IDetector interface for the ReCamera platform.
 * 
 * This implementation uses the SSCMA library to run the YOLOv11n-seg model on the
 * ReCamera TPU hardware.
 */
class RecameraDetector : public IDetector {
public:
    /**
     * @brief Construct a new RecameraDetector object
     * 
     * @param modelPath Path to the YOLOv11n-seg model file (cvimodel format)
     * @param namesPath Path to class names file (typically coco.names)
     * @param confThreshold Confidence threshold for detections
     */
    RecameraDetector(const std::string& modelPath, const std::string& namesPath, float confThreshold = 0.5f);
    
    /**
     * @brief Destroy the RecameraDetector object
     */
    virtual ~RecameraDetector();
    
    /**
     * @brief Initialize the detector
     * 
     * @return true if initialization succeeded
     * @return false if initialization failed
     */
    bool initialize() override;
    
    /**
     * @brief Detect humans in the input image
     * 
     * @param img Input image (BGR format)
     * @param detections Output vector of detected bounding boxes
     * @return true if detection succeeded
     * @return false if detection failed
     */
    bool detect(const cv::Mat& img, std::vector<Detection>& detections) override;
    
    /**
     * @brief Get the model input size
     * 
     * @return cv::Size Model input dimensions (width, height)
     */
    cv::Size getInputSize() const override;
    
    /**
     * @brief Get the person class ID
     * 
     * @return int Person class ID (0 for COCO dataset)
     */
    int getPersonClassId() const override;
    
    /**
     * @brief Get the class names supported by this detector
     * 
     * @return std::vector<std::string> Class names
     */
    const std::vector<std::string> &getClassNames() const override;
    
private:
    /// Path to the YOLO model file
    std::string modelPath;
    
    /// Path to the class names file
    std::string namesPath;
    
    /// Confidence threshold for detections
    float confThreshold;
    
    /// Model initialization status
    bool isInitialized;
    
    /// SSCMA model handle (void* to avoid including SSCMA headers here)
    void* modelHandle;
    
    /// Class names parsed from the namesPath file
    std::vector<std::string> classNames;
    
    /// Person class ID (typically 0 in COCO)
    int personClassId;
};

#endif // RECAMERA_DETECTOR_H
