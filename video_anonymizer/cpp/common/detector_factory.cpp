#include "detector_factory.h"

// Include platform-specific implementations
#ifdef TARGET_RECAMERA
#include "../recamera_project/recamera_detector.h"
#else
#include "../default_project/yoloscpp_detector.h"
#endif

std::unique_ptr<IDetector> DetectorFactory::createDetector(const Parameters& params) {
#ifdef TARGET_RECAMERA
    // Create RecameraDetector for ReCamera platform
    return std::make_unique<RecameraDetector>(
        params.modelPath.empty() ? "yolo11n-seg.cvimodel" : params.modelPath,
        params.labelsPath,
        params.confidenceThreshold
    );
#else
    // Create YolosCppDetector for x86_64 platform
    return std::make_unique<YolosCppDetector>(params);
#endif
}
