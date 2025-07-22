#ifndef DETECTOR_FACTORY_H
#define DETECTOR_FACTORY_H

#include <memory>
#include <string>
#include "idetector.h"

// Forward declarations of detector implementations
class RecameraDetector;
class YolosCppDetector;

/**
 * @brief Factory class for creating the appropriate detector implementation based on platform
 */
class DetectorFactory {
public:
    /**
     * @brief Parameters for detector creation and configuration
     */
    struct Parameters {
        std::string modelPath;         ///< Path to the model file (ONNX, TensorRT, CVITEK etc.)
        std::string labelsPath;        ///< Path to the class labels file
        float confidenceThreshold;     ///< Confidence threshold for detections [0.0-1.0]
        float iouThreshold;            ///< IoU threshold for NMS [0.0-1.0]
        bool useGPU;                   ///< Whether to use GPU for inference (if available)
        bool debugMode;                ///< Whether to enable debug mode
        // Constructor with default values
        Parameters() 
            : modelPath(""),
              labelsPath("coco.names"),
              confidenceThreshold(0.5f),
              iouThreshold(0.45f),
              useGPU(false),
              debugMode(false) {}
    };
    
    /**
     * @brief Create a detector based on the current platform
     * 
     * Will create the appropriate detector implementation based on the current platform:
     * - On ReCamera (TARGET_RECAMERA defined): Creates a RecameraDetector
     * - Otherwise: Creates a YolosCppDetector
     * 
     * @param params Parameters for the detector configuration
     * @return std::unique_ptr<IDetector> Platform-specific detector implementation
     */
    static std::unique_ptr<IDetector> createDetector(const Parameters& params = Parameters());
};

#endif // DETECTOR_FACTORY_H
