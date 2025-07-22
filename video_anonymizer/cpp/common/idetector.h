#ifndef IDETECTOR_H
#define IDETECTOR_H

#include <vector>
#include <opencv2/core.hpp>
#include <string>

/**
 * @brief Interface for object detectors in the Video Anonymizer
 * 
 * This interface defines the contract for all detector implementations,
 * allowing for seamless switching between different detector backends
 * without changing the application code.
 */
class IDetector {
public:
    /**
     * @brief Detection result structure representing a detected object
     */
    struct Detection {
        cv::Rect bbox;         ///< Bounding box in pixel coordinates
        float confidence;      ///< Detection confidence [0-1]
        int classId;           ///< Class ID (0 = person in COCO)
        cv::Mat mask;          ///< Segmentation mask (if available)
        
        // Default constructor
        Detection() : bbox(), confidence(0.0f), classId(-1), mask() {}
        
        // Constructor with bbox and confidence
        Detection(const cv::Rect& bbox, float confidence, int classId)
            : bbox(bbox), confidence(confidence), classId(classId), mask() {}
    };
    
    /**
     * @brief Virtual destructor
     */
    virtual ~IDetector() = default;
    
    /**
     * @brief Initialize the detector
     * 
     * This should be called before the first detect() call to prepare
     * the model and associated resources.
     * 
     * @return true if initialization succeeded
     * @return false if initialization failed
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Detect objects in an image
     * 
     * @param img Input image (BGR format)
     * @param detections Output vector of detected objects
     * @return true if detection succeeded
     * @return false if detection failed
     */
    virtual bool detect(const cv::Mat& img, 
                        std::vector<Detection>& detections) = 0;
    
    /**
     * @brief Get the input size required by the model
     * 
     * @return cv::Size Model input dimensions (width, height)
     */
    virtual cv::Size getInputSize() const = 0;

    /**
     * @brief Get the person class ID
     * 
     * @return int Person class ID
     */
    virtual int getPersonClassId() const = 0;

    /**
     * @brief Get the class names supported by this detector
     * 
     * @return std::vector<std::string> Class names
     */
    virtual const std::vector<std::string> &getClassNames() const = 0;
};

#endif // IDETECTOR_H
