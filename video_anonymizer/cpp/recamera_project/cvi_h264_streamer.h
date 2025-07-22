#ifndef CVI_H264_STREAMER_H
#define CVI_H264_STREAMER_H

#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <opencv2/core.hpp>
#include <queue>
#include <condition_variable>
#include "cvi_system.h"

// Sophgo CVI SDK headers
extern "C" {
    #include "linux/cvi_common.h"
    #include "cvi_type.h"
    #include "cvi_venc.h"
    #include "cvi_sys.h"
    #include "cvi_vb.h"
    #include "rtsp.h"
}

/**
 * @brief Class for H264 encoding and RTSP streaming of OpenCV images
 * 
 * This class handles the conversion of OpenCV images to H264 streams
 * and sends them to connected RTSP clients. It uses the Sophgo CVI SDK
 * for hardware-accelerated H264 encoding and RTSP streaming.
 */
class CviH264Streamer {
public:
    /**
     * @brief Configuration for the H264 encoder and RTSP streamer
     */
    struct Config {
        // RTSP settings
        int port;                // RTSP server port
        std::string streamName;  // Stream name/path
        std::string username;    // Username for RTSP authentication (optional)
        std::string password;    // Password for RTSP authentication (optional)
        
        // Video settings
        int width;               // Video width
        int height;              // Video height
        int fps;                 // Target framerate
        int bitrate;             // Target bitrate in bits/sec (4 Mbps default)
        int gop;                 // GOP size (typically equals fps for 1-second GOP)
        
        // H264 specific settings
        int profile;             // H264 profile (0=Baseline, 1=Main, 2=High)
        int rcMode;              // Rate control mode (0=CBR, 1=VBR, 2=AVBR, 3=FIXQP)
        
        // Buffer settings
        int vbPoolCount;         // Number of video buffers to allocate
        
        // Quality settings
        int qpMin;               // Minimum QP value
        int qpMax;               // Maximum QP value
        int qpInit;              // Initial QP value for I-frames

        // Channel settings
        int vencChannel;         // VENC channel to use. Tested with values between 0 to 8.
        video_ch_index_t videoCh;             // Video channel to use?

        // Constructor with default values
        Config() : 
            port(8554),
            streamName("live"),
            username(""),
            password(""),
            width(1280),
            height(720),
            fps(30),
            bitrate(4000000),
            gop(30),
            profile(0),
            rcMode(0),
            vbPoolCount(8),
            qpMin(20),
            qpMax(45),
            qpInit(30),
            vencChannel(0),
            videoCh(VIDEO_CH1) {}
    };
    
    /**
     * @brief Constructor
     * 
     * @param config Configuration parameters for the streamer
     */
    CviH264Streamer(const Config& config = Config());
    
    /**
     * @brief Destructor
     */
    ~CviH264Streamer();
    
    /**
     * @brief Initialize the encoder and RTSP server
     * 
     * @param sysInitializer Optional system initializer to use instead of creating our own
     * @return true if initialization was successful, false otherwise
     */
    bool initialize(bool perform_video_init=true, bool configure_vbpool=true, bool start_thread=true);
    
    /**
     * @brief Check if the streamer is initialized
     * 
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;
    
    /**
     * @brief Stop the streamer
     */
    void stop();

    /**
     * @brief Send a frame to be encoded and streamed
     * 
     * Takes an OpenCV image (BGR format), encodes it to H264,
     * and sends it to any connected RTSP clients.
     * 
     * @param frame OpenCV Mat in BGR format
     * @param blocking If true, wait for queue space instead of dropping frames when queue is full
     * @return true if the frame was successfully processed and sent
     */
    bool sendFrame(const cv::Mat& frame, bool blocking = false);
    
    /**
     * @brief Get the RTSP URL for this stream
     * 
     * @return String containing the full RTSP URL
     */
    std::string getStreamUrl() const;
    
    /**
     * @brief Get the number of currently connected clients
     * 
     * @return Number of active RTSP connections
     */
    int getClientCount() const;
    
    /**
     * @brief Force generation of an I-frame (keyframe)
     * 
     * This can be useful when starting a new recording or when
     * a new client connects to the stream.
     */
    void forceIFrame();

private:
    // Disable copy constructor and assignment operator
    CviH264Streamer(const CviH264Streamer&) = delete;
    CviH264Streamer& operator=(const CviH264Streamer&) = delete;
    
    /**
     * @brief Get all available IP addresses from network interfaces
     * 
     * @return A formatted string with all network interfaces and their URLs
     */
    std::string getAvailableIpAddresses() const;
    
    /**
     * @brief Initialize the VENC encoder
     */
    bool initVenc();
    
    /**
     * @brief Initialize the RTSP server
     */
    bool initRtsp();
    
    /**
     * @brief Create a YUV frame from OpenCV BGR/Gray image
     */
    bool createYuvFrame(const cv::Mat& frame, VIDEO_FRAME_INFO_S* pstFrame);
    
    /**
     * @brief Send encoded H264 data to RTSP server
     */
    bool sendEncodedDataToRtsp(VENC_STREAM_S* pstStream);
    
    /**
     * @brief Release all allocated resources
     */
    void cleanup();
    
    // Configuration
    Config m_config;
    
    // Stream information
    std::string m_streamUrl;
    
    // State variables
    bool m_initialized;
    std::atomic<bool> m_running;
    
    // Thread safety
    mutable std::mutex m_mutex;
    
    // CVI SDK handles and channels
    VENC_CHN m_vencChn;
    
    // RTSP context
    struct {
        CVI_S32 session_cnt;
        CVI_S32 port;
        CVI_BOOL bStart[8];  // Support up to 8 sessions
        VENC_CHN VencChn[8];
        CVI_RTSP_SESSION *pstSession[8];
        CVI_RTSP_SESSION_ATTR SessionAttr[8];
        CVI_RTSP_STATE_LISTENER listener;
        CVI_RTSP_CTX *pstServerCtx;
        pthread_mutex_t mutex;
    } m_rtspCtx;
    
    // Statistics
    std::atomic<uint64_t> m_frameCount;
    std::atomic<uint64_t> m_errorCount;
    std::atomic<int> m_clientCount;
    
    // Bitrate statistics
    std::atomic<uint64_t> m_totalBytes;
    std::atomic<uint64_t> m_totalFrames;
    std::atomic<uint64_t> m_totalIFrames;
    
    // Time tracking
    uint64_t m_lastIFrameTime;
    uint64_t m_startTime;
    
    // Threading support
    std::thread m_encodingThread;
    std::mutex m_frameMutex;
    std::condition_variable m_frameCondition;
    std::queue<cv::Mat> m_frameQueue;
    size_t m_maxQueueSize;
    bool m_threadRunning;
    
    // Thread function for encoding
    void encodingThreadFunc();
    
    // Process a single frame (used by thread)
    bool processFrame(const cv::Mat& frame);
};

#endif // CVI_H264_STREAMER_H 