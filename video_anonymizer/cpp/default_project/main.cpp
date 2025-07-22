#include "../common/video_anonymizer.h"
#include "../common/detector_factory.h"
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>  // For std::setprecision and formatting
#include <csignal>  // For signal handling (SIGINT, signal())
#include <atomic>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
    std::cout << "  -i, --input <path>        Input video file or camera index (default: 0)" << std::endl;
    std::cout << "  -o, --output <path>       Output video file (optional)" << std::endl;
    std::cout << "  -m, --model <path>        YOLO model path (default: platform-specific)" << std::endl;
    std::cout << "  -l, --learning <rate>     Learning rate for background model (default: 0.2)" << std::endl;
    std::cout << "  -c, --conf <threshold>    Confidence threshold (0.0-1.0, default: 0.2)" << std::endl;
    std::cout << "      --iou  <threshold>    IoU threshold (0.0-1.0, default: 0.45)" << std::endl;
    std::cout << "  -w, --width <pixels>      Maximum width to process (preserves aspect ratio)" << std::endl;
    std::cout << "  -g, --gui                 Enable GUI mode. Display the anonymized video" << std::endl;
    std::cout << "  -d, --debug               Enable debug mode. Shows detection and background masks" << std::endl;
    std::cout << "  --use-gpu                 Use GPU for inference" << std::endl;
}



std::atomic<bool> gStopFlag(false);

void signal_handler(int signal) {
    gStopFlag.store(true);
}
 

int main(int argc, char* argv[]) {
    // Default parameters
    std::string inputSource = "0";          // Default to camera 0
    std::string outputPath = "";           // Empty means no output file
    std::string modelPath = "yolo11n-seg.onnx";            // Empty means use default for platform
    std::string labelsPath = "coco.names";
    float confThreshold = 0.2f;            // Default confidence threshold
    float iouThreshold = 0.45f;            // Default IoU threshold
    float learningRate = 0.2f;             // Default learning rate
    int maxWidth = 0;                      // 0 means no resizing
    bool enableGui = false;                // GUI mode disabled by default
    bool enableDebug = false;              // Debug mode disabled by default
    bool useGPU = false;                   // Use GPU for inference
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-i" || arg == "--input") {
            if (i + 1 < argc) inputSource = argv[++i];
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) outputPath = argv[++i];
        } else if (arg == "-m" || arg == "--model") {
            if (i + 1 < argc) modelPath = argv[++i];
        } else if (arg == "-l" || arg == "--learning") {
            if (i + 1 < argc) learningRate = std::stof(argv[++i]);
        } else if (arg == "-c" || arg == "--conf") {
            if (i + 1 < argc) confThreshold = std::stof(argv[++i]);
        } else if (arg == "--iou") {
            if (i + 1 < argc) iouThreshold = std::stof(argv[++i]);
        } else if (arg == "-w" || arg == "--width") {
            if (i + 1 < argc) maxWidth = std::stoi(argv[++i]);
        } else if (arg == "-g" || arg == "--gui") {
            enableGui = true;
        } else if (arg == "-d" || arg == "--debug") {
            enableDebug = true;
        } else if (arg == "--use-gpu") {
            useGPU = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Try to parse input source as a number for camera index
    int cameraIndex = -1;
    try {
        cameraIndex = std::stoi(inputSource);
    } catch (const std::exception&) {
        // Not a number, treat as file path
    }
    
    // Open video capture
    cv::VideoCapture cap;
    if (cameraIndex >= 0) {
        cap.open(cameraIndex);
    } else {
        cap.open(inputSource);
    }
    
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open video source: " << inputSource << std::endl;
        return 1;
    }
    
    // Get video properties
    int frameWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frameHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    
    // If fps is invalid (0), set a default
    if (fps <= 0) fps = 30.0;
    
    // Calculate resize factor if maxWidth is specified
    double resizeFactor = 1.0;
    if (maxWidth > 0 && frameWidth > maxWidth) {
        resizeFactor = static_cast<double>(maxWidth) / frameWidth;
        frameWidth = maxWidth;
        frameHeight = static_cast<int>(frameHeight * resizeFactor);
    }
    
    // Create video writer if output path is specified
    cv::VideoWriter writer;
    if (!outputPath.empty()) {
        int fourcc = cv::VideoWriter::fourcc('X', 'V', 'I', 'D');
        writer.open(outputPath, fourcc, fps, cv::Size(frameWidth, frameHeight));
        
        if (!writer.isOpened()) {
            std::cerr << "Error: Could not create output video file: " << outputPath << std::endl;
            return 1;
        }
    }
    
    // Configure and initialize video anonymizer
    VideoAnonymizer::Parameters params;
    params.confThreshold = confThreshold;
    params.iouThreshold = iouThreshold;
    params.modelPath = modelPath;
    params.labelsPath = labelsPath;
    params.learningRate = learningRate;
    params.useGPU = useGPU;
    params.debugMode = enableDebug;
    
    VideoAnonymizer anonymizer(params);
    
    // Create windows if GUI or debug mode is enabled
    if (enableGui || enableDebug) {
        cv::namedWindow("Anonymized Video", cv::WINDOW_NORMAL);
        cv::resizeWindow("Anonymized Video", frameWidth, frameHeight);
        
        if (enableDebug) {
            cv::namedWindow("Background Model", cv::WINDOW_NORMAL);
            cv::resizeWindow("Background Model", frameWidth, frameHeight);
            cv::namedWindow("Detections", cv::WINDOW_NORMAL);
            cv::resizeWindow("Detections", frameWidth, frameHeight);
            cv::namedWindow("Mask", cv::WINDOW_NORMAL);
            cv::resizeWindow("Mask", frameWidth, frameHeight);
        }
    }
    
    // Detect sigint signal (ctrl+c) and set a flag if pressed
    std::signal(SIGINT, signal_handler);

    
    // Main processing loop
    while (!gStopFlag) {
        // Read frame
        cv::Mat frame;
        if (!cap.read(frame)) {
            break;
        }
        
        // Resize if needed
        if (resizeFactor != 1.0) {
            cv::resize(frame, frame, cv::Size(), resizeFactor, resizeFactor);
        }
        
        // Process frame for anonymization
        auto startTime = std::chrono::high_resolution_clock::now();
        cv::Mat anonymized = anonymizer.processFrame(frame);
        auto endTime = std::chrono::high_resolution_clock::now();
        
        // Calculate processing time
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        double processingFps = 1000.0 / duration;
        
        // Write to output file if specified
        if (writer.isOpened()) {
            writer.write(anonymized);
        }
        
        // Display if GUI or debug mode is enabled
        if (enableGui || enableDebug) {
            // Add FPS text to the frame
            std::stringstream fpsText;
            fpsText << std::fixed << std::setprecision(1) << "FPS: " << processingFps;
            cv::putText(anonymized, fpsText.str(), cv::Point(10, 30), 
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
            
            // Show the anonymized video
            cv::imshow("Anonymized Video", anonymized);

            // In debug mode, the video_anonymizer and human_detector will take care of show additional windows
            
            // Check for key press
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q') { // ESC or 'q' key
                break;
            }
        }
    }
    
    // Release resources
    cap.release();
    if (writer.isOpened()) {
        writer.release();
    }
    
    // Close all windows
    if (enableGui || enableDebug) {
        cv::destroyAllWindows();
    }
    
    return 0;
}
