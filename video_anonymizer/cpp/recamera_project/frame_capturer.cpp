#include "frame_capturer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

// Static pointer to the instance for signal handler
static FrameCapturer* g_instance = nullptr;

// Get singleton instance
FrameCapturer& FrameCapturer::getInstance(int width, int height, int fps, video_ch_index_t video_channel) {
    static FrameCapturer instance(width, height, fps, video_channel);
    return instance;
}

// Constructor (private)
FrameCapturer::FrameCapturer(int width, int height, int fps, video_ch_index_t video_channel)
    : width_(width)
    , height_(height)
    , fps_(fps)
    , initialized_(false)
    , running_(false)
    , stop_requested_(false)
    , frame_callback_(nullptr)
    , new_frame_available_(false)
    , latest_timestamp_(0)
    , video_channel_(video_channel)
{
    // Register as the global instance for signal handling
    g_instance = this;

    // Initialize video parameters
    memset(&video_params_, 0, sizeof(video_params_));
    video_params_.format = VIDEO_FORMAT_NV21;
    video_params_.width = width_;
    video_params_.height = height_;
    video_params_.fps = fps_;
}

// Destructor
FrameCapturer::~FrameCapturer() {
    stop();
    
    if (initialized_) {
        deinitVideo(false);
        initialized_ = false;
    }
    
    // Unregister as the global instance
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

// Initialize the camera and video pipeline
bool FrameCapturer::initialize() {
    if (initialized_) {
        std::cerr << "FrameCapturer is already initialized" << std::endl;
        return true;
    }

    // Initialize video system (without venc)
    if (initVideo(false) != 0) {
        std::cerr << "Failed to initialize video system" << std::endl;
        return false;
    }

    // Setup video channel
    if (setupVideo(video_channel_, &video_params_, false) != 0) {
        std::cerr << "Failed to setup video channel" << std::endl;
        return false;
    }

    initialized_ = true;
    return true;
}

// Start capturing frames
bool FrameCapturer::start() {
    if (!initialized_) {
        std::cerr << "FrameCapturer is not initialized" << std::endl;
        return false;
    }

    if (running_) {
        std::cerr << "FrameCapturer is already running" << std::endl;
        return true;
    }

    // Start video pipeline
    if (startVideo(false) != 0) {
        std::cerr << "Failed to start video pipeline" << std::endl;
        return false;
    }

    running_ = true;
    stop_requested_ = false;
    
    // Start frame capture thread
    capture_thread_ = std::thread(&FrameCapturer::captureThread, this);
    
    // Wait for 1 second to ensure the camera is ready
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    return true;
}

// Frame capture thread function
void FrameCapturer::captureThread() {
    cv::Mat frame;
    struct timeval tv;
    uint64_t timestamp;
    
    // Set thread priority
    struct sched_param param;
    param.sched_priority = 80;
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    
    while (!stop_requested_) {
        // Get video frame with timeout (shorter than 1/fps to ensure we don't miss frames)
        int timeout_ms = std::min(100, 1000 / fps_ / 2);
        
        if (getVideoFrame(video_channel_, frame, timeout_ms)) {
            // Get current timestamp
            gettimeofday(&tv, NULL);
            timestamp = tv.tv_sec * 1000 + tv.tv_usec / 1000; // milliseconds
            
            // Store the frame
            {
                std::lock_guard<std::mutex> lock(frame_mutex_);
                frame.copyTo(latest_frame_);
                latest_timestamp_ = timestamp;
                new_frame_available_ = true;
            }
            frame_cv_.notify_all();
            
            // Call user callback if registered
            if (frame_callback_) {
                frame_callback_(frame, timestamp);
            }
        }
        
        // Sleep to match the desired frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps_));
    }
}

// Stop capturing frames
bool FrameCapturer::stop() {
    if (!running_) {
        return true;
    }

    stop_requested_ = true;
    
    // Wait for capture thread to finish
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    
    // Stop video pipeline
    deinitVideo(false);
    
    running_ = false;
    
    // Notify any waiting threads
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        new_frame_available_ = true;
    }
    frame_cv_.notify_all();
    
    return true;
}

// Get the latest frame
bool FrameCapturer::getFrame(cv::Mat& frame, int timeout_ms) {
    if (!running_) {
        std::cerr << "FrameCapturer is not running" << std::endl;
        return false;
    }

    std::unique_lock<std::mutex> lock(frame_mutex_);
    
    if (!new_frame_available_) {
        // Wait for a new frame with timeout
        if (frame_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                             [this] { return new_frame_available_ || stop_requested_; })) {
            if (stop_requested_) {
                return false;
            }
        } else {
            // Timeout reached
            return false;
        }
    }
    
    // Copy the latest frame
    latest_frame_.copyTo(frame);
    new_frame_available_ = false;
    
    return true;
}

// Register a callback to be called when a new frame is captured
void FrameCapturer::registerFrameCallback(FrameCallback callback) {
    frame_callback_ = callback;
}

// Set the frames per second
bool FrameCapturer::setFPS(int fps) {
    if (running_) {
        std::cerr << "Cannot change FPS while FrameCapturer is running" << std::endl;
        return false;
    }
    
    fps_ = fps;
    video_params_.fps = fps;
    
    return true;
}

// Get the current frames per second
int FrameCapturer::getFPS() const {
    return fps_;
}

// Check if the capturer is initialized
bool FrameCapturer::isInitialized() const {
    return initialized_;
}

// Check if the capturer is running
bool FrameCapturer::isRunning() const {
    return running_;
}

