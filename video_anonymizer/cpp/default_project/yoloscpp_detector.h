#pragma once

#include "idetector.h"
#include "../common/detector_factory.h"
#include <memory>
#include <string>
#include <vector>

// Forward declaration to avoid including YOLOs-CPP headers in client code
class YOLOv11SegDetector;

/**
 * @brief HumanDetector class encapsulates human detection and segmentation functionality
 * using the YOLOv11 segmentation model from YOLOs-CPP library.
 */
class YolosCppDetector : public IDetector {
public:

    /**
     * @brief Constructor
     * @param params Parameters for the detector
     */
    explicit YolosCppDetector(const DetectorFactory::Parameters& params = DetectorFactory::Parameters());

    /**
     * @brief Destructor
     */
    ~YolosCppDetector() override;

    bool initialize() override;

    bool detect(const cv::Mat& img, std::vector<Detection>& detections) override;

    cv::Size getInputSize() const override;

    int getPersonClassId() const override;

    const std::vector<std::string> &getClassNames() const override;

private:
    DetectorFactory::Parameters mParams;
    std::unique_ptr<YOLOv11SegDetector> mSegmentor;
    int mPersonClassNumber;
};
