#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include "cvi_system.h"

/**
 * @brief A singleton class to capture frames from the camera at a specified rate
 * 
 * This class encapsulates the functionality of initializing the camera,
 * setting up the video pipeline, and capturing frames at a specified rate.
 */
class FrameCapturer {
public:
    /**
     * @brief Callback function signature for frame processing
     * 
     * @param frame The captured frame as an OpenCV Mat
     * @param timestamp The timestamp of the frame capture
     * @return true if the frame was processed successfully, false otherwise
     */
    using FrameCallback = std::function<bool(const cv::Mat&, uint64_t timestamp)>;

    /**
     * @brief Get the singleton instance of FrameCapturer
     * 
     * @param width The width of the frames to capture
     * @param height The height of the frames to capture
     * @param fps The frames per second to capture
     * @param video_channel The video channel to capture from
     * @return FrameCapturer& Reference to the singleton instance
     */
    static FrameCapturer& getInstance(int width = 1920, int height = 1080, int fps = 30, video_ch_index_t video_channel = VIDEO_CH0);

    /**
     * @brief Destructor
     */
    ~FrameCapturer();

    /**
     * @brief Configure the CVI system
     * 
     * @return true if configuration was successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Start capturing frames
     * 
     * @return true if frame capture was started successfully, false otherwise
     */
    bool start();

    /**
     * @brief Stop capturing frames
     * 
     * @return true if frame capture was stopped successfully, false otherwise
     */
    bool stop();

    /**
     * @brief Get the latest frame
     * 
     * @param frame The output frame as an OpenCV Mat
     * @param timeout_ms The timeout in milliseconds to wait for a frame
     * @return true if a frame was captured, false otherwise
     */
    bool getFrame(cv::Mat& frame, int timeout_ms = 1000);

    /**
     * @brief Register a callback to be called when a new frame is captured
     * 
     * @param callback The callback function to register
     */
    void registerFrameCallback(FrameCallback callback);

    /**
     * @brief Set the frames per second
     * 
     * @param fps The frames per second to set
     * @return true if the fps was set successfully, false otherwise
     */
    bool setFPS(int fps);

    /**
     * @brief Get the current frames per second
     * 
     * @return int The current frames per second
     */
    int getFPS() const;

    /**
     * @brief Check if the capturer is initialized
     * 
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;

    /**
     * @brief Check if the capturer is running
     * 
     * @return true if running, false otherwise
     */
    bool isRunning() const;

private:
    // Private constructor for singleton pattern
    FrameCapturer(int width = 1920, int height = 1080, int fps = 30, video_ch_index_t video_channel = VIDEO_CH0);
    
    // Deleted copy constructor and assignment operator
    FrameCapturer(const FrameCapturer&) = delete;
    FrameCapturer& operator=(const FrameCapturer&) = delete;
    
    // Frame capture thread function
    void captureThread();

    // Internal state
    int width_;
    int height_;
    int fps_;
    bool initialized_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
    
    // Frame callback
    FrameCallback frame_callback_;
    
    // Latest frame
    cv::Mat latest_frame_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    bool new_frame_available_;
    uint64_t latest_timestamp_;

    // Video parameters
    video_ch_param_t video_params_;
    
    // Frame capture thread
    std::thread capture_thread_;
    
    // Video channel to capture from
    video_ch_index_t video_channel_;
}; 