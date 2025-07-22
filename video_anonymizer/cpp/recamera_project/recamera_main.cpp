#include <iostream>
#include <string>
#include <chrono>
#include <iomanip> // For std::setprecision and formatting
#include <thread>
#include <signal.h>
#include <atomic>

#include <opencv2/opencv.hpp>
#include "../common/video_anonymizer.h" // Use the common VideoAnonymizer
#include "frame_capturer.h"
#include "cvi_h264_streamer.h"

#define TAG "recamera_main"

// Global flag to indicate when the application should exit
std::atomic<bool> g_running(true);
std::unique_ptr<VideoAnonymizer> g_anonymizer;

// Signal handler for Ctrl+C
void signalHandler(int signum) {
    std::cout << std::endl << "Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    g_running.store(false);
    
    // Don't exit immediately - let the main loop handle cleanup
    // Reset the signal handler to default
    signal(signum, SIG_DFL);
}

// Frame callback function that processes frames and sends them to the RTSP streamer
bool frameCallback(const cv::Mat& frame, uint64_t timestamp, cv::Mat& processedFrame) {
    static int frameCount = 0;
    static auto lastStatsTime = std::chrono::high_resolution_clock::now();
    static int statFrameCount = 0;
    
    if (!g_running.load()) {
        return false;
    }
    
    if (frame.empty()) {
        std::cerr << "Empty frame received" << std::endl;
        return false;
    }
    
    // Process frame with the anonymizer if available
    auto startTime = std::chrono::high_resolution_clock::now();
    
    if (g_anonymizer) {
        try {
            processedFrame = g_anonymizer->processFrame(frame);
        } catch (const std::exception& e) {
            std::cerr << "Error processing frame: " << e.what() << std::endl;
            processedFrame = frame.clone();
        }
    } else {
        processedFrame = frame.clone();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    // Add some visual elements
    std::string timeText = "Frame: " + std::to_string(frameCount++) + 
                          " | Process time: " + std::to_string(duration) + " ms";
    
    // Draw the timestamp field
    cv::rectangle(processedFrame, cv::Point(0, 0), cv::Point(500, 50), cv::Scalar(0, 0, 255), cv::FILLED);
    cv::putText(processedFrame, timeText, cv::Point(10, 30), 
               cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);
    
    // Update statistics
    statFrameCount++;
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatsTime).count();
    
    if (elapsed >= 5) {
        float fps = statFrameCount / static_cast<float>(elapsed);
        std::cout << "Processing rate: " << std::fixed << std::setprecision(1) << fps 
                  << " FPS, latency: " << duration << " ms" << std::endl;
        statFrameCount = 0;
        lastStatsTime = now;
    }
    
    return true;
}

int main(int argc, char** argv) {
    // Set up signal handling for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Define command line arguments using OpenCV's parser
    const std::string keys =
        "{help h usage ? |      | print this message}"
        "{@model         |      | path to the YOLO model file (required)}"
        "{conf           | 0.2  | confidence threshold}"
        "{width          | 1920 | capture width}"
        "{height         | 1080 | capture height}"
        "{fps            | 10   | capture FPS}"
        "{port           | 8554 | RTSP port}"
        "{name           | live | RTSP stream name}"
        "{username       | admin| RTSP username}"
        "{password       | admin| RTSP password}"
        "{disable_rtsp   |      | Disable RTSP streaming}"
        "{disable_anonymization |      | Disable anonymization}"
        "{bitrate        | 4000000| Bitrate in bps}"
        "{gop            | 10     | GOP in frames}"
        "{vbPoolCount    | 8     | VB pool count}"
        "{rcMode         | 3      | Rate control mode (0=CBR, 1=VBR, 2=AVBR, 3=FIXQP)}"
        "{qpMin          | 15     | QP min}"
        "{qpMax          | 30     | QP max}"
        "{qpInit         | 20     | QP init}"
        "{profile        | 1      | Profile (0=Baseline, 1=Main, 2=High)}";

    // Parse command line arguments
    cv::CommandLineParser parser(argc, argv, keys);
    parser.about("ReCamera Anonymizer - Blur people in video streams");

    // Show help if requested or if required parameters are missing
    if (parser.has("help") || (!parser.has("@model") && !parser.has("disable_anonymization"))) {
        parser.printMessage();
        return 0;
    }

    // Get parameters with defaults
    std::string modelPath = parser.get<std::string>("@model");
    float confThreshold = parser.get<float>("conf");
    int captureWidth = parser.get<int>("width");
    int captureHeight = parser.get<int>("height");
    int captureFps = parser.get<int>("fps");
    int streamPort = parser.get<int>("port");
    std::string streamName = parser.get<std::string>("name");
    std::string username = parser.get<std::string>("username");
    std::string password = parser.get<std::string>("password");
    bool disableRtsp = parser.has("disable_rtsp");
    bool disableAnonymization = parser.has("disable_anonymization");
    int bitrate = parser.get<int>("bitrate");
    int gop = parser.get<int>("gop");
    int vbPoolCount = parser.get<int>("vbPoolCount");
    int rcMode = parser.get<int>("rcMode");
    int qpMin = parser.get<int>("qpMin");
    int qpMax = parser.get<int>("qpMax");
    int qpInit = parser.get<int>("qpInit");
    int profile = parser.get<int>("profile");

    // Check for parsing errors
    if (!parser.check()) {
        parser.printErrors();
        return 1;
    }

    // Display current settings
    std::cout << "Model path: " << (disableAnonymization ? "Disabled" : modelPath) << std::endl;
    std::cout << "Confidence threshold: " << confThreshold << std::endl;
    std::cout << "Capture settings: " << captureWidth << "x" << captureHeight 
              << " @ " << captureFps << " FPS" << std::endl;
    std::cout << "RTSP streaming on port " << streamPort 
              << ", stream name: " << streamName << std::endl;




    // --- Initialize Camera ---
    std::cout << "Initializing frame capturer..." << std::endl;
    FrameCapturer& capturer = FrameCapturer::getInstance(captureWidth, captureHeight, captureFps, VIDEO_CH0);
    
    if (!capturer.initialize()) {
        std::cerr << "Error: Could not initialize FrameCapturer" << std::endl;
        return 1;
    }
    
    std::cout << "Frame capturer initialized successfully" << std::endl;


    // --- Configure H264 Streamer ---
    CviH264Streamer::Config streamerConfig;
    streamerConfig.port = streamPort;
    streamerConfig.streamName = streamName;
    streamerConfig.username = username;
    streamerConfig.password = password;
    streamerConfig.width = captureWidth;
    streamerConfig.height = captureHeight;
    streamerConfig.fps = captureFps;
    streamerConfig.bitrate = bitrate; // 2 Mbps
    streamerConfig.videoCh = VIDEO_CH0;
    streamerConfig.gop = gop;  // 1 second GOP
    streamerConfig.profile = profile;  // Main profile
    streamerConfig.vencChannel = 0;
    streamerConfig.vbPoolCount = vbPoolCount;
    
    streamerConfig.rcMode = rcMode;             // Fixed QP mode for better quality control
    streamerConfig.qpMin = qpMin;             // Higher min QP for better encoding speed
    streamerConfig.qpMax = qpMax;             // Standard max QP
    streamerConfig.qpInit = qpInit;            // Starting quality level 

    
    std::cout << "Initializing H264 streamer..." << std::endl;
    CviH264Streamer streamer(streamerConfig);

    if (!disableRtsp) {
        // Configure the streamer
        std::cout << "Configuring H264 streamer" << std::endl;
        if (!streamer.initialize(false, true, false)) {
            std::cerr << "Failed to initialize H264 streamer" << std::endl;
            return 1;
        }
        
        std::cout << "H264 Streamer configured successfully" << std::endl;
    }

    // --- Initialize VideoAnonymizer if not disabled ---
    if (!disableAnonymization) {
        try {
            // Configure VideoAnonymizer parameters
            VideoAnonymizer::Parameters params;
            params.modelPath = modelPath;
            params.labelsPath = "coco.names";
            params.confThreshold = confThreshold;
            params.iouThreshold = 0.45f;
            params.learningRate = 0.01f;
            params.useGPU = false;
            params.debugMode = false;
            
            // Create the anonymizer
            g_anonymizer = std::make_unique<VideoAnonymizer>(params);
            std::cout << "Video anonymizer initialized successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize video anonymizer: " << e.what() << std::endl;
            std::cerr << "Continuing without anonymization" << std::endl;
            disableAnonymization = true;
        }
    } else {
        std::cout << "Anonymization disabled by command line option" << std::endl;
    }


    int frameCount = 0;
    // Set up frame callback function
    auto callback = [&streamer, disableRtsp, &frameCount](const cv::Mat& frame, uint64_t timestamp) -> bool {
        cv::Mat processedFrame;
        // Process the frame with our global callback
        bool processed = frameCallback(frame, timestamp, processedFrame);
        
        // Send the processed frame to RTSP streamer if enabled
        if (processed) {
            frameCount++;
            if (!disableRtsp) {
                return streamer.sendFrame(processedFrame);
            }
        }
        
        return processed;
    };
    
    capturer.registerFrameCallback(callback);
    
    // Start frame capture
    std::cout << "Starting continuous frame capture and processing..." << std::endl;
    if (!capturer.start()) {
        std::cerr << "Error: Could not start frame capture" << std::endl;
        return 1;
    }

    if (!disableRtsp) {
        // Start the streamer
        std::cout << "Starting RTSP streamer" << std::endl;
        if (!streamer.initialize(false, false, true)) {
            std::cerr << "Failed to start RTSP streamer" << std::endl;
            return 1;
        }
        std::cout << "RTSP streamer started" << std::endl;
        std::cout << "Connect to one of these URLs with any RTSP client (e.g., VLC):" << std::endl;
        std::cout << streamer.getStreamUrl() << std::endl;
    }

    std::cout << "Press Ctrl+C to exit" << std::endl;
    
    auto startTime = std::chrono::steady_clock::now();
    auto lastStatusTime = startTime;
    // Main loop - keep running until signal received
    while (g_running.load()) {
        // Print client count and status every 5 seconds
        auto now = std::chrono::steady_clock::now();
        
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusTime).count() >= 5) {
            std::cout << "RTSP clients connected: " << streamer.getClientCount() << '\n';
            std::cout << "Frame count: " << frameCount << '\n';
            std::cout << "FPS: " << frameCount / std::chrono::duration<double>(now - startTime).count() << '\n';
            lastStatusTime = now;
        }
        
        // Sleep to avoid excessive CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Shutting down..." << std::endl;


    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Clean up resources
    std::cout << "Shutting down and releasing resources..." << std::endl;
    
    // Stop frame capture
    capturer.stop();
    
    // Stop RTSP streaming
    if (!disableRtsp) {
        streamer.stop();
    }
    
    // Release anonymizer
    g_anonymizer.reset();
    
    std::cout << "Exited gracefully" << std::endl;
    return 0;
}
