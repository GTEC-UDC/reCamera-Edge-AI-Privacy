#include "recamera_detector.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <opencv2/imgproc.hpp>

// Include SSCMA headers
#include <sscma.h>

#define TAG "RecameraDetector"

// Helper function for image preprocessing
cv::Mat preprocessImageWithPadding(cv::Mat& image, ma::Model* model) {
    int ih = image.rows;
    int iw = image.cols;
    int oh = 0;
    int ow = 0;
    
    assert (model->getInputType() == MA_INPUT_TYPE_IMAGE);
    oh = reinterpret_cast<const ma_img_t*>(model->getInput())->height;
    ow = reinterpret_cast<const ma_img_t*>(model->getInput())->width;
    //std::cout << TAG << ": Resizing input image (" << iw << "x" << ih << ") to (" << ow << "x" << oh << ")" << std::endl;
    
    cv::Mat resizedImage;
    double resize_scale = std::min((double)oh / ih, (double)ow / iw);
    int nh = (int)(ih * resize_scale);
    int nw = (int)(iw * resize_scale);
    cv::resize(image, resizedImage, cv::Size(nw, nh));
    int top    = (oh - nh) / 2;
    int bottom = (oh - nh) - top;
    int left   = (ow - nw) / 2;
    int right  = (ow - nw) - left;
    
    cv::Mat paddedImage;
    cv::copyMakeBorder(resizedImage, paddedImage, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::cvtColor(paddedImage, paddedImage, cv::COLOR_BGR2RGB);
    
    return paddedImage;
}


cv::Mat preprocessImageResizeOnly(cv::Mat& image, ma::Model* model) {
    int ih = image.rows;
    int iw = image.cols;
    int oh = 0;
    int ow = 0;
    
    assert (model->getInputType() == MA_INPUT_TYPE_IMAGE);
    oh = reinterpret_cast<const ma_img_t*>(model->getInput())->height;
    ow = reinterpret_cast<const ma_img_t*>(model->getInput())->width;
    //std::cout << TAG << ": Resizing input image (" << iw << "x" << ih << ") to (" << ow << "x" << oh << ")" << std::endl;
    
    cv::Mat resizedImage;
    cv::resize(image, resizedImage, cv::Size(ow, oh));
    
    return resizedImage;
}
    
/**
 * Constructor
 */
RecameraDetector::RecameraDetector(const std::string& modelPath, const std::string& namesPath, float confThreshold)
    : modelPath(modelPath), 
      namesPath(namesPath), 
      confThreshold(confThreshold),
      isInitialized(false),
      modelHandle(nullptr),
      personClassId(0) {
    
    std::cout << TAG << ": Creating detector with model: " << modelPath << std::endl;
    std::cout << TAG << ": Class names file: " << namesPath << std::endl;
    std::cout << TAG << ": Confidence threshold: " << confThreshold << std::endl;
    
    // Load class names from file if available
    try {
        std::ifstream file(namesPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    classNames.push_back(line);
                }
            }
            file.close();
            
            std::cout << TAG << ": Loaded " << classNames.size() << " class names" << std::endl;
            
            // Find the person class ID
            auto it = std::find(classNames.begin(), classNames.end(), "person");
            if (it != classNames.end()) {
                personClassId = static_cast<int>(std::distance(classNames.begin(), it));
                std::cout << TAG << ": Person class found at index: " << personClassId << std::endl;
            } else {
                std::cout << TAG << ": Person class not found, using default ID 0" << std::endl;
            }
        } else {
            std::cerr << TAG << ": Warning: Could not open class names file: " << namesPath << std::endl;
            // Add a default "person" class
            classNames.push_back("person");
        }
    } catch (const std::exception& e) {
        std::cerr << TAG << ": Error loading class names: " << e.what() << std::endl;
        // Add a default "person" class
        classNames.push_back("person");
    }
}

/**
 * Destructor
 */
RecameraDetector::~RecameraDetector() {
    // Clean up SSCMA resources
    if (modelHandle) {
        try {
            std::cout << TAG << ": Cleaning up model resources" << std::endl;
            // Clean up SSCMA resources
            ma::Model* model = static_cast<ma::Model*>(modelHandle);
            ma::ModelFactory::remove(model);
            modelHandle = nullptr;
        } catch (const std::exception& e) {
            std::cerr << TAG << ": Exception during model cleanup: " << e.what() << std::endl;
        }
    }
}

/**
 * Initialize the detector
 */
bool RecameraDetector::initialize() {
    if (isInitialized) {
        std::cout << TAG << ": Already initialized" << std::endl;
        return true; // Already initialized
    }
    
    std::cout << TAG << ": Initializing detector..." << std::endl;
    
    try {
        ma_err_t ret = MA_OK;
        
        // Initialize SSCMA engine
        std::cout << TAG << ": Creating CVI engine" << std::endl;
        auto* engine = new ma::engine::EngineCVI();
        
        std::cout << TAG << ": Initializing engine" << std::endl;
        ret = engine->init();
        if (ret != MA_OK) {
            std::cerr << TAG << ": Engine init failed with error: " << (int)ret << std::endl;
            delete engine;
            return false;
        }
        
        // Load the model
        std::cout << TAG << ": Loading model from: " << modelPath << std::endl;
        ret = engine->load(modelPath.c_str());
        if (ret != MA_OK) {
            std::cerr << TAG << ": Engine load model failed with error: " << (int)ret << std::endl;
            
            // Check if the model file exists
            std::ifstream modelFile(modelPath);
            if (!modelFile.good()) {
                std::cerr << TAG << ": Model file does not exist or cannot be accessed" << std::endl;
            } else {
                std::cerr << TAG << ": Model file exists but couldn't be loaded, size: " 
                          << modelFile.tellg() << " bytes" << std::endl;
            }
            
            delete engine;
            return false;
        }
        
        // Create model
        std::cout << TAG << ": Creating model from engine" << std::endl;
        ma::Model* model = ma::ModelFactory::create(engine);
        if (model == nullptr) {
            std::cerr << TAG << ": Model not supported by the engine" << std::endl;
            delete engine;
            return false;
        }
        
        // Verify model type
        std::cout << TAG << ": Verifying model input type" << std::endl;
        if (model->getInputType() != MA_INPUT_TYPE_IMAGE) {
            std::cerr << TAG << ": Model input type not supported, type: " << model->getInputType() << std::endl;
            ma::ModelFactory::remove(model);
            return false;
        }
        
        // Save model to handle
        modelHandle = static_cast<void*>(model);
        isInitialized = true;
        
        // Display model information
        std::cout << TAG << ": Model info:" << std::endl;
        std::cout << TAG << ":   - Input type: " << model->getInputType() << std::endl;
        std::cout << TAG << ":   - Output type: " << model->getOutputType() << std::endl;
        
        if (model->getInputType() == MA_INPUT_TYPE_IMAGE) {
            const ma_img_t* img = reinterpret_cast<const ma_img_t*>(model->getInput());
            std::cout << TAG << ":   - Input dimensions: " << img->width << "x" << img->height << std::endl;
        }
        
        // Log success
        std::cout << TAG << ": Model loaded and initialized successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << TAG << ": Exception during initialization: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Detect humans in an image
 */
bool RecameraDetector::detect(const cv::Mat& img, std::vector<Detection>& detections) {
    if (!isInitialized && !initialize()) {
        std::cerr << TAG << ": Cannot detect - detector not initialized" << std::endl;
        return false;
    }
    
    std::cout << TAG << ": Processing frame of size " << img.cols << "x" << img.rows << std::endl;
    
    try {
        // Clear previous detections
        detections.clear();
        
        // Get our model from the handle
        ma::Model* model = static_cast<ma::Model*>(modelHandle);
        
        // Preprocess the image for the model
        //cv::Mat processedImg = preprocessImageWithPadding(const_cast<cv::Mat&>(img), model);   // TODO: This step could be avoided.
        cv::Mat processedImg = preprocessImageResizeOnly(const_cast<cv::Mat&>(img), model);   // TODO: This step could be avoided.

        std::cout << TAG << ": Preprocessed image size: " << processedImg.size() << std::endl;
        
        // Setup image for SSCMA
        ma_img_t sscmaImg;
        sscmaImg.data = processedImg.data;
        sscmaImg.size = processedImg.rows * processedImg.cols * processedImg.channels();
        sscmaImg.width = processedImg.cols;
        sscmaImg.height = processedImg.rows;
        sscmaImg.format = MA_PIXEL_FORMAT_RGB888;
        sscmaImg.rotate = MA_PIXEL_ROTATE_0;
        
        std::cout << TAG << ": Running inference" << std::endl;

        // Run detection based on model type
        if (model->getOutputType() == MA_OUTPUT_TYPE_SEGMENT) {
            // For segmentation models (YOLO with masks)
            ma::model::Segmentor* segmentor = static_cast<ma::model::Segmentor*>(model);
            
            // Set confidence threshold
            std::cout << TAG << ": Setting confidence threshold: " << confThreshold << std::endl;
            segmentor->setConfig(MA_MODEL_CFG_OPT_THRESHOLD, confThreshold);
            
            // Run detection
            std::cout << TAG << ": Running segmentation model" << std::endl;
            segmentor->run(&sscmaImg);
            
            // Get results
            std::cout << TAG << ": Getting results" << std::endl;
            auto results = segmentor->getResults();
            if (results.empty()) {
                std::cout << TAG << ": No results found" << std::endl;
            }
            
            // Original image dimensions for scaling back
            float orig_width = static_cast<float>(img.cols);
            float orig_height = static_cast<float>(img.rows);
            
            // Convert SSCMA results to our IDetector format
            for (auto& result : results) {
                std::cout << TAG << ": Processing result for class " << result.box.target 
                          << " with score " << result.box.score << std::endl;
                          
                // Only process person class or all classes if needed
                if (result.box.target == personClassId) {
                    std::cout << TAG << ": Found person with confidence " << result.box.score << std::endl;
                    Detection det;
            
                    // Convert normalized coordinates to pixel coordinates
                    float x = result.box.x * orig_width;
                    float y = result.box.y * orig_height;
                    float w = result.box.w * orig_width;
                    float h = result.box.h * orig_height;
                    
                    // Create bounding box
                    det.bbox = cv::Rect(
                        static_cast<int>(x - w/2),
                        static_cast<int>(y - h/2),
                        static_cast<int>(w),
                        static_cast<int>(h)
                    );
                    
                    det.confidence = result.box.score;
                    det.classId = result.box.target;
                    
                    std::cout << TAG << ": Bounding box: " << det.bbox << std::endl;
                    
                    // Create mask from segmentation data
                    if (!result.mask.data.empty()) {
                        std::cout << TAG << ": Processing mask with dims " << result.mask.width << "x" << result.mask.height << '\n';

                        // Measure the time it takes for each step
                        auto start = std::chrono::high_resolution_clock::now();
                        
                        // First create mask at the native mask resolution
                        cv::Mat nativeMask = cv::Mat::zeros(result.mask.height, result.mask.width, CV_8UC1);

                        // Convert bit-packed mask to full mask at native resolution
                        // TODO: would it be faster to use a LUT?
                        for (int i = 0; i < result.mask.height; ++i) {
                            for (int j = 0; j < result.mask.width; ++j) {
                                // Check if the bit is set in the packed mask
                                int byte_idx = i * result.mask.width / 8 + j / 8;
                                int bit_idx = j % 8;
                                
                                if (byte_idx < result.mask.data.size() && 
                                    (result.mask.data[byte_idx] & (1 << bit_idx))) {
                                    nativeMask.at<uchar>(i, j) = 255;
                                }
                            }
                        }

                        auto mask_creation_end = std::chrono::high_resolution_clock::now();
                        auto mask_creation_duration = std::chrono::duration_cast<std::chrono::milliseconds>(mask_creation_end - start);
                        std::cout << TAG << ": Mask creation time: " << mask_creation_duration.count() << "ms\n";

                        // Resize the mask to match the original image size
                        cv::Mat mask;
                        if (nativeMask.rows != img.rows || nativeMask.cols != img.cols) {
                            cv::resize(nativeMask, mask, cv::Size(img.cols, img.rows), 0, 0, cv::INTER_NEAREST);
                        } else {
                            mask = nativeMask;
                        }
                        
                        auto mask_resize_end = std::chrono::high_resolution_clock::now();
                        auto mask_resize_duration = std::chrono::duration_cast<std::chrono::milliseconds>(mask_resize_end - mask_creation_end);
                        std::cout << TAG << ": Mask resize time: " << mask_resize_duration.count() << "ms\n";

                        // Apply some dilation to improve the mask
                        /*
                        cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
                        cv::dilate(mask, mask, element);

                        auto mask_dilate_end = std::chrono::high_resolution_clock::now();
                        auto mask_dilate_duration = std::chrono::duration_cast<std::chrono::milliseconds>(mask_dilate_end - mask_resize_end);
                        std::cout << TAG << ": Mask dilation time: " << mask_dilate_duration.count() << "ms\n";
                        */
                       
                        det.mask = mask;
                        //std::cout << TAG << ": Mask created with non-zero pixels: " 
                        //          << cv::countNonZero(mask) << std::endl;
                    } else {
                        std::cout << TAG << ": No mask data available" << std::endl;
                    }
                    
                    detections.push_back(det);
                } else {
                    std::cout << TAG << ": Skipping non-person class " << result.box.target << std::endl;
                }
            }
        } 
        else if (model->getOutputType() == MA_OUTPUT_TYPE_BBOX) {
            // For regular detection models (without masks)
            ma::model::Detector* detector = static_cast<ma::model::Detector*>(model);
            
            // Set confidence threshold
            detector->setConfig(MA_MODEL_CFG_OPT_THRESHOLD, confThreshold);
            
            // Run detection
            detector->run(&sscmaImg);
            
            // Get results
            auto results = detector->getResults();
            
            // Original image dimensions for scaling back
            float orig_width = static_cast<float>(img.cols);
            float orig_height = static_cast<float>(img.rows);
            
            // Convert SSCMA results to our IDetector format
            for (auto& result : results) {
                // Only process person class or all classes if needed
                if (result.target == personClassId) {
                    Detection det;
                    
                    // Convert normalized coordinates to pixel coordinates
                    float x = result.x * orig_width;
                    float y = result.y * orig_height;
                    float w = result.w * orig_width;
                    float h = result.h * orig_height;
                    
                    // Create bounding box
                    det.bbox = cv::Rect(
                        static_cast<int>(x - w/2),
                        static_cast<int>(y - h/2),
                        static_cast<int>(w),
                        static_cast<int>(h)
                    );
                    
                    det.confidence = result.score;
                    det.classId = result.target;
                    
                    detections.push_back(det);
                }
            }
        }
        
        // Log performance metrics
        auto perf = model->getPerf();
        std::cout << TAG << ": Performance: preprocess=" << perf.preprocess << "ms, inference=" 
                  << perf.inference << "ms, postprocess=" << perf.postprocess << "ms" << std::endl;
        std::cout << TAG << ": Total detections found: " << detections.size() << std::endl;
        
        std::cout << TAG << ": Detection successful with " << detections.size() << " people found" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << TAG << ": Exception during detection: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Get the input size required by the model
 */
cv::Size RecameraDetector::getInputSize() const {
    if (!isInitialized || !modelHandle) {
        return cv::Size(640, 640); // Default size
    }
    
    try {
        ma::Model* model = static_cast<ma::Model*>(modelHandle);
        
        if (model->getInputType() == MA_INPUT_TYPE_IMAGE) {
            const ma_img_t* img = reinterpret_cast<const ma_img_t*>(model->getInput());
            return cv::Size(img->width, img->height);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception getting input size: " << e.what() << std::endl;
    }
    
    return cv::Size(640, 640); // Default size
}

/**
 * Get the person class ID
 */
int RecameraDetector::getPersonClassId() const {
    return personClassId;
}

/**
 * Get the class names
 */
const std::vector<std::string>& RecameraDetector::getClassNames() const {
    return classNames;
}
