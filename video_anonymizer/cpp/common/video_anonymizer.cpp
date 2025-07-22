#include "video_anonymizer.h"
#include <opencv2/imgproc.hpp>
#include <iostream>

#include "detector_factory.h"

VideoAnonymizer::VideoAnonymizer(const Parameters& params)
    : mParams(params), mFrameCount(0), mLastDetectionMask(cv::Mat()), mLastDetections(), mBackground(cv::Mat()) {
    
    // Initialize human detector
    DetectorFactory::Parameters detectorParams;
    detectorParams.modelPath = params.modelPath;
    detectorParams.labelsPath = params.labelsPath;
    detectorParams.confidenceThreshold = params.confThreshold;
    detectorParams.iouThreshold = params.iouThreshold;
    detectorParams.useGPU = params.useGPU;
    detectorParams.debugMode = params.debugMode;
    
    mDetector = DetectorFactory::createDetector(detectorParams);
    
    if (!mDetector || !mDetector->initialize()) {
        std::cerr << "Failed to initialize detector" << std::endl;
        throw std::runtime_error("Detector initialization failed");
    }
}

VideoAnonymizer::~VideoAnonymizer() = default;

void VideoAnonymizer::updateBackground(const cv::Mat& frame, const cv::Mat& humanMask) {
    // If background is not initialized, initialize it with a grey image of the same size
    if (mBackground.empty()) {
        //mBackground = cv::Mat(frame.rows, frame.cols, CV_32FC(frame.channels()), cv::Scalar(127, 127, 127));
        mBackground = cv::Mat(frame.rows, frame.cols, CV_8UC(frame.channels()), cv::Scalar(127, 127, 127));
    }
    
    // Create a mask for areas without humans (inverse of humanMask)
    cv::Mat bgUpdateMask;
    cv::bitwise_not(humanMask, bgUpdateMask);
    
    // Apply learning rate to update the background
    // For areas without humans: bg = (1-alpha) * bg + alpha * frame
    // accumulateWeighted updates mBackground in-place
    try {
        cv::Mat backgroundFloat;
        mBackground.convertTo(backgroundFloat, CV_32FC(frame.channels()));
        cv::accumulateWeighted(frame, backgroundFloat, mParams.learningRate, bgUpdateMask);
        backgroundFloat.convertTo(mBackground,CV_8UC(frame.channels()));
    } catch (const std::exception &e) {
        std::cerr << "Exception at accumulateWeighted: " << e.what() << std::endl;
#ifndef TARGET_RECAMERA
        std::cerr << "frame: channels " << frame.channels() 
                  << " type " << cv::typeToString(frame.type())
                  << " size " << frame.size << std::endl;
        std::cerr << "mBackground: channels " << mBackground.channels() 
                  << " type " << cv::typeToString(frame.type())
                  << " size " << mBackground.size << std::endl;
        std::cerr << "bgUpdateMask: channels " << bgUpdateMask.channels() 
                  << " type " << cv::typeToString(frame.type())
                  << " size " << bgUpdateMask.size << std::endl;
#endif
    }
}

cv::Mat VideoAnonymizer::processFrame(const cv::Mat& frame) {
    if (frame.empty()) {
        return frame;
    }
    
    // Make a copy of the original frame
    cv::Mat original = frame.clone();
    cv::Mat result;
    
    // Detect humans in the frame
    cv::Mat humanMask;
    detectHumans(original, humanMask);
    
    // If we don't have a background yet, initialize it with the current frame
    /*if (mBackground.empty()) {
        std::cout << "Frame " << mFrameCount << " background is empty, initializing with original frame" << std::endl;
        mBackground = original.clone();
        mFrameCount++;
        return original;
    }*/

    //std::cout << "Frame " << mFrameCount << " humanMask size: " << humanMask.size() << std::endl; //<< " non-zero pixels: " << cv::countNonZero(humanMask) << std::endl;
    
    // Create a combined mask for anonymization
    //cv::Mat combinedMask = createCombinedMask(frame, humanMask);   // TODO: very very slow in reCamera!
    cv::Mat combinedMask = humanMask;
    
    //std::cout << "Frame " << mFrameCount << " combinedMask size: " << combinedMask.size() << std::endl; //<< " non-zero pixels: " << cv::countNonZero(combinedMask) << std::endl;
    
    // Update the background model with the current frame
    updateBackground(original, combinedMask);

    //std::cout << "Frame " << mFrameCount << " background updated" << std::endl;
    
    // Apply the mask to the frame
    result = applyMaskToFrame(frame, combinedMask);

    //std::cout << "Frame " << mFrameCount << " result size: " << result.size() << std::endl;
    
    // Increment frame counter
    mFrameCount++;
    
    // If in debug mode, show the masks and detection results
    if (mParams.debugMode) {
#ifndef TARGET_RECAMERA
        // Create debug visualization
        cv::Mat debugView;
        if (original.type() != result.type()) {
            std::cerr << "Original and result frames have different types: " << original.type() << " != " << result.type() << std::endl;
            return result;
        }
        cv::hconcat(original, result, debugView);
        
        // Scale down if too large
        if (debugView.cols > 1280) {
            cv::resize(debugView, debugView, cv::Size(), 0.5, 0.5);
        }
        
        // Show the background model
        cv::imshow("Background Model", mBackground);
        
        // Show the combined mask
        cv::imshow("Mask", combinedMask);
#endif
    }
    
    return result;
}

cv::Mat VideoAnonymizer::createMask(const cv::Mat& frame, const std::vector<IDetector::Detection>& detections) {
    // Create an empty mask
    cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
    
    // Process each detection
    for (const auto& det : detections) {
        // Only put humans in the mask
        if (det.classId != mDetector->getPersonClassId()) {
            continue;
        }
        
        // If we have a mask from segmentation, use it
        if (!det.mask.empty()) {
            // Add the segmentation mask to our combined mask
            cv::bitwise_or(mask, det.mask, mask);
        } else {
            // If no mask available, use the bounding box
            cv::rectangle(mask, det.bbox, cv::Scalar(255), -1); // Fill the box
        }
    }
    
    return mask;
}


void VideoAnonymizer::detectHumans(const cv::Mat& frame, cv::Mat& mask) {
    // Use the detector to detect humans
    std::vector<IDetector::Detection> detections;
    bool success = mDetector->detect(frame, detections);
    
    if (!success) {
        std::cerr << "Human detection failed" << std::endl;
        return;
    }
    
    // Create a mask from the detection results
    mask = createMask(frame, detections);
    
    // Store the mask for debug visualization
    mLastDetectionMask = mask.clone();
    mLastDetections = detections;
}

cv::Mat VideoAnonymizer::createCombinedMask(const cv::Mat& frame, const cv::Mat& mask) {
    // Calculate dilation size based on frame dimensions
    int frameSize = std::min(frame.cols, frame.rows);
    float dilationFactor = mFrameCount < mParams.warmupFrames ? mParams.warmupDilationFactor : mParams.dilationFactor;
    int dilationSize = static_cast<int>(frameSize * dilationFactor);
    
    // Clamp dilation size to min/max values
    dilationSize = std::max(mParams.minDilation, std::min(dilationSize, mParams.maxDilation));
    
    // Create structuring element for dilation
    cv::Mat element = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(2 * dilationSize + 1, 2 * dilationSize + 1),
        cv::Point(dilationSize, dilationSize)
    );
    
    // Dilate the mask
    cv::Mat dilatedMask;
    cv::dilate(mask, dilatedMask, element, cv::Point(-1, -1), mParams.dilationIterations);
    
    return dilatedMask;
}

cv::Mat VideoAnonymizer::applyMaskToFrame(const cv::Mat& frame, const cv::Mat& mask) {
    // If background is empty, return original frame
    if (mBackground.empty()) {
        return frame.clone();
    }
    
    // Create result frame
    cv::Mat result = frame.clone();
    
    // Apply the mask to replace human pixels with background
    mBackground.copyTo(result, mask);
    
    return result;
}

cv::Mat VideoAnonymizer::getBackground() const {
    return mBackground;
}

cv::Mat VideoAnonymizer::getDetectionMask() const {
    return mLastDetectionMask;
}

const std::vector<IDetector::Detection>& VideoAnonymizer::getDetections() const {
    return mLastDetections;
}

void VideoAnonymizer::reset() {
    // Reset frame counter
    mFrameCount = 0;
    
    // Clear masks
    mLastDetectionMask = cv::Mat();
    mBackground = cv::Mat();
    
    // Clear detections
    mLastDetections.clear();
}
