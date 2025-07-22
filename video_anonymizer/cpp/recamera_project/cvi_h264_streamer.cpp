#include "cvi_h264_streamer.h"
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <iomanip>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <thread>

#define TAG "CviH264Streamer"
#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#define REPORT_BITRATE_INTERVAL_MS 10000

// Helper function to get current time in milliseconds
static uint64_t getCurrentTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Constructor
CviH264Streamer::CviH264Streamer(const Config& config)
    : m_config(config),
      m_initialized(false),
      m_running(false),
      m_vencChn(-1),
      m_frameCount(0),
      m_errorCount(0),
      m_clientCount(0),
      m_totalBytes(0),
      m_totalFrames(0),
      m_totalIFrames(0),
      m_lastIFrameTime(0),
      m_startTime(0),
      m_maxQueueSize(10),
      m_threadRunning(false) {
    
    // Validate configuration parameters
    if (m_config.width <= 0 || m_config.height <= 0) {
        std::cerr << TAG << ": Invalid dimensions, using defaults (1280x720)" << std::endl;
        m_config.width = 1280;
        m_config.height = 720;
    }
    
    if (m_config.fps <= 0 || m_config.fps > 60) {
        std::cerr << TAG << ": Invalid FPS, using default (30)" << std::endl;
        m_config.fps = 30;
    }
    
    if (m_config.bitrate <= 0) {
        // Calculate a reasonable default bitrate based on resolution
        int defaultBitrate = m_config.width * m_config.height * m_config.fps * 0.1;
        m_config.bitrate = std::min(std::max(defaultBitrate, 1000000), 10000000); // Between 1 and 10 Mbps
        std::cerr << TAG << ": Invalid bitrate, using calculated value: " << m_config.bitrate << " bps" << std::endl;
    }
    
    // Update GOP if needed (typically equals framerate for 1-second GOP)
    if (m_config.gop <= 0) {
        m_config.gop = m_config.fps;
    }
    
    // Get IP addresses for RTSP URL
    std::string available_urls = getAvailableIpAddresses();
    
    // Create URL for RTSP stream
    std::string auth = "";
    if (!m_config.username.empty() && !m_config.password.empty()) {
        auth = m_config.username + ":" + m_config.password + "@";
    }
    
    // First create a placeholder URL with [] to be replaced with actual IPs
    m_streamUrl = "rtsp://" + auth + "ADDRESS" + ":" + 
                 std::to_string(m_config.port) + "/" + m_config.streamName;
    
    // Only show resolution and bitrate info in constructor log
    std::cout << TAG << ": Resolution: " << m_config.width << "x" << m_config.height 
              << " @ " << m_config.fps << " FPS, Bitrate: " << (m_config.bitrate / 1000) << " Kbps" << std::endl;
    
    // Initialize RTSP context
    memset(&m_rtspCtx, 0, sizeof(m_rtspCtx));
    m_rtspCtx.session_cnt = 1;
    m_rtspCtx.port = m_config.port;
    m_rtspCtx.VencChn[0] = m_config.vencChannel;
    
    // Generate stream URL
    std::string ipInfo = getAvailableIpAddresses();
    m_streamUrl = ipInfo;
}

// Get available IP addresses from all interfaces
std::string CviH264Streamer::getAvailableIpAddresses() const {
    std::string result;
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return "Error getting IP addresses";
    }

    // Walk through linked list, maintaining head pointer
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        family = ifa->ifa_addr->sa_family;

        // Only interested in IPv4 and not loopback
        if (family == AF_INET && 
            strcmp(ifa->ifa_name, "lo") != 0) {
            
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                           host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
            
            if (s != 0) {
                continue; // Skip if couldn't get address
            }
            
            // Format the URL for this interface
            std::string url = "rtsp://";
            if (!m_config.username.empty() && !m_config.password.empty()) {
                url += m_config.username + ":" + m_config.password + "@";
            }
            url += std::string(host) + ":" + std::to_string(m_config.port) + "/" + m_config.streamName;
            
            result += "- " + std::string(ifa->ifa_name) + ": " + url + "\n";
        }
    }

    freeifaddrs(ifaddr);
    return result;
}

// Destructor
CviH264Streamer::~CviH264Streamer() {
    // Print final statistics before cleanup
    if (m_totalFrames > 0) {
        double avgBytesPerFrame = static_cast<double>(m_totalBytes) / m_totalFrames;
        double avgBitsPerSecond = 0;
        uint64_t endTime = getCurrentTimeMs();
        
        if (endTime > m_startTime) {
            double durationSec = (endTime - m_startTime) / 1000.0;
            avgBitsPerSecond = (m_totalBytes * 8.0) / durationSec;
            
            std::cout << "\n";
            std::cout << "========== FINAL ENCODING STATISTICS ==========\n";
            std::cout << "Total encoded frames: " << m_totalFrames << "\n";
            std::cout << "Total I-frames: " << m_totalIFrames << " (" 
                      << std::fixed << std::setprecision(1) 
                      << (static_cast<double>(m_totalIFrames) / m_totalFrames * 100.0) << "%)\n";
            std::cout << "Total encoded bytes: " << m_totalBytes << "\n";
            std::cout << "Average bytes per frame: " << static_cast<int>(avgBytesPerFrame) << "\n";
            std::cout << "Average bitrate: " << std::fixed << std::setprecision(2) 
                      << (avgBitsPerSecond / 1000000.0) << " Mbps\n";
            std::cout << "Target bitrate: " << std::fixed << std::setprecision(2)
                      << (m_config.bitrate / 1000000.0) << " Mbps\n";
            std::cout << "Encoding duration: " << std::fixed << std::setprecision(1) 
                      << durationSec << " seconds\n";
            std::cout << "Average FPS: " << std::fixed << std::setprecision(1) 
                      << (m_totalFrames / durationSec) << "\n";
            std::cout << "===============================================\n";
        }
    }
    
    cleanup();
}

// Initialize the encoder and RTSP server
bool CviH264Streamer::initialize(bool perform_video_init, bool configure_vbpool, bool start_thread) {
    if (m_initialized) {
        std::cout << TAG << ": Already initialized" << std::endl;
        return true;
    }
    
    // Initialize system components
    if (perform_video_init) {
        initVideo(false);
    }

    if (configure_vbpool) {

        video_ch_param_t param;
        param.width = m_config.width;
        param.height = m_config.height;
        param.fps = m_config.fps;
        param.format = VIDEO_FORMAT_H264;

        /*if (setupVideo(m_config.videoCh, &param, false) != 0) {
            std::cerr << "Failed to setup video channel" << std::endl;
            return false;
        }*/

        if (cvi_system_setVbPool(m_config.videoCh, &param, m_config.vbPoolCount) != CVI_SUCCESS) {
            std::cerr << TAG << ": Failed to set VbPool: cvi_system_setVbPool for channel " << m_config.videoCh 
                      << ". vbPoolCount: " << m_config.vbPoolCount << std::endl;
            return false;
        }

        if (cvi_system_Sys_Init() != CVI_SUCCESS) {
            std::cerr << TAG << ": Failed to initialize system: cvi_system_Sys_Init" << std::endl;
            return false;
        }

        //startVideo(false);
    }

    if (!start_thread) {
        std::cout << TAG << ": Skipping the starting of RTSP server thread" << std::endl;
        return true;
    }
    
    std::cout << TAG << ": Starting H264 encoder and RTSP server..." << std::endl;
    
    m_running.store(true);
    m_startTime = getCurrentTimeMs();
    
    try {
        // Initialize VENC (H264 encoder)
        if (!initVenc()) {
            std::cerr << TAG << ": Failed to initialize VENC" << std::endl;
            cleanup();
            return false;
        }
        
        // Initialize RTSP server last
        if (!initRtsp()) {
            std::cerr << TAG << ": Failed to initialize RTSP server" << std::endl;
            cleanup();
            return false;
        }
        
        // Start the encoding thread with higher priority
        m_threadRunning = true;
        m_encodingThread = std::thread(&CviH264Streamer::encodingThreadFunc, this);
        
        // Set thread priority (if possible)
        if (m_encodingThread.native_handle()) {
            // Get the current thread attributes
            pthread_attr_t attr;
            struct sched_param param;
            
            // Initialize with default attributes
            pthread_attr_init(&attr);
            
            // Get scheduling parameters
            pthread_attr_getschedparam(&attr, &param);
            
            // Set to real-time priority
            param.sched_priority = sched_get_priority_max(SCHED_RR) / 2;
            
            // Set thread scheduling parameters
            if (pthread_setschedparam(pthread_self(), SCHED_RR, &param) != 0) {
                std::cerr << TAG << ": Warning: Could not set thread priority, continuing with default" << std::endl;
            } else {
                std::cout << TAG << ": Encoding thread priority increased" << std::endl;
            }
            
            // Clean up
            pthread_attr_destroy(&attr);
        }
        
        m_initialized = true;
        std::cout << TAG << ": Initialization successful" << std::endl;
        return true;
    } 
    catch (const std::exception& e) {
        std::cerr << TAG << ": Exception during initialization: " << e.what() << std::endl;
        cleanup();
        return false;
    }
}

// Stop the streamer
void CviH264Streamer::stop() {
    cleanup();
}

// Get client count
int CviH264Streamer::getClientCount() const {
    return m_clientCount.load();
}

// Force generation of an I-frame
void CviH264Streamer::forceIFrame() {
    if (!m_initialized || m_vencChn < 0) {
        return;
    }
    
    // We don't need to lock the mutex here since this method is called from
    // sendFrame() which already holds the lock, or from the RTSP connect callback
    // which is in a different thread. CVI_VENC_RequestIDR is thread-safe by itself.
    
    // Request an IDR frame
    CVI_VENC_RequestIDR(m_vencChn, CVI_TRUE);
    m_lastIFrameTime = getCurrentTimeMs();
}

// Initialize the VENC encoder
bool CviH264Streamer::initVenc() {
    CVI_S32 s32Ret;
    
    // Use the configured channel for our encoder
    m_vencChn = m_config.vencChannel;
    
    // Validate channel number (CVI SDK typically supports 0-31)
    if (m_vencChn < 0 || m_vencChn > 31) {
        std::cerr << TAG << ": Invalid VENC channel number: " << m_vencChn << ", using default (0)" << std::endl;
        m_vencChn = 0;
    }
    
    // Log the channel being used
    std::cout << TAG << ": Using VENC channel " << m_vencChn << std::endl;
    
    // Set up encoder attributes
    VENC_CHN_ATTR_S stVencChnAttr;
    memset(&stVencChnAttr, 0, sizeof(VENC_CHN_ATTR_S));
    
    // Configure as H.264
    stVencChnAttr.stVencAttr.enType = PT_H264;
    
    // Set picture dimensions
    stVencChnAttr.stVencAttr.u32MaxPicWidth = m_config.width;
    stVencChnAttr.stVencAttr.u32MaxPicHeight = m_config.height;
    stVencChnAttr.stVencAttr.u32PicWidth = m_config.width;
    stVencChnAttr.stVencAttr.u32PicHeight = m_config.height;
    
    // Set profile based on config (0=Baseline, 1=Main, 2=High)
    stVencChnAttr.stVencAttr.u32Profile = m_config.profile;
    
    // Set buffer size with alignment
    uint32_t bufSize = m_config.width * m_config.height * 3 / 2;  // YUV420
    bufSize = ALIGN_UP(bufSize, 1024);
    stVencChnAttr.stVencAttr.u32BufSize = bufSize;
    
    // Configure GOP structure - NORMALP is standard P-frame GOP
    stVencChnAttr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    stVencChnAttr.stGopAttr.stNormalP.s32IPQpDelta = 3;  // Delta QP between I and P frames
    
    // Configure rate control based on config
    switch (m_config.rcMode) {
        case 1:  // VBR mode
            stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
            stVencChnAttr.stRcAttr.stH264Vbr.u32Gop = m_config.gop;
            stVencChnAttr.stRcAttr.stH264Vbr.u32SrcFrameRate = m_config.fps;
            stVencChnAttr.stRcAttr.stH264Vbr.fr32DstFrameRate = m_config.fps;
            // Convert from bits/sec to kbps
            stVencChnAttr.stRcAttr.stH264Vbr.u32MaxBitRate = m_config.bitrate / 1000;
            std::cout << TAG << ": Set VBR mode with Max Bitrate: " << (m_config.bitrate / 1000) << " Kbps" << std::endl;
            break;
            
        case 2:  // AVBR mode
            stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264AVBR;
            stVencChnAttr.stRcAttr.stH264AVbr.u32Gop = m_config.gop;
            stVencChnAttr.stRcAttr.stH264AVbr.u32SrcFrameRate = m_config.fps;
            stVencChnAttr.stRcAttr.stH264AVbr.fr32DstFrameRate = m_config.fps;
            // Convert from bits/sec to kbps
            stVencChnAttr.stRcAttr.stH264AVbr.u32MaxBitRate = m_config.bitrate / 1000;
            std::cout << TAG << ": Set AVBR mode with Max Bitrate: " << (m_config.bitrate / 1000) << " Kbps" << std::endl;
            break;
            
        case 3:  // FIXQP mode - this forces specific QP values
            stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264FIXQP;
            stVencChnAttr.stRcAttr.stH264FixQp.u32Gop = m_config.gop;
            stVencChnAttr.stRcAttr.stH264FixQp.u32SrcFrameRate = m_config.fps;
            stVencChnAttr.stRcAttr.stH264FixQp.fr32DstFrameRate = m_config.fps;
            stVencChnAttr.stRcAttr.stH264FixQp.u32IQp = m_config.qpInit;  // I-frame QP
            stVencChnAttr.stRcAttr.stH264FixQp.u32PQp = m_config.qpInit + 3;  // P-frame QP
            std::cout << TAG << ": Set FIXQP mode with QP values: I=" << m_config.qpInit 
                      << ", P=" << (m_config.qpInit + 3) << std::endl;
            break;
            
        case 0:  // CBR mode (default)
        default:
            stVencChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
            stVencChnAttr.stRcAttr.stH264Cbr.u32Gop = m_config.gop;
            stVencChnAttr.stRcAttr.stH264Cbr.u32SrcFrameRate = m_config.fps;
            stVencChnAttr.stRcAttr.stH264Cbr.fr32DstFrameRate = m_config.fps;
            // Convert from bits/sec to kbps
            stVencChnAttr.stRcAttr.stH264Cbr.u32BitRate = m_config.bitrate / 1000;
            std::cout << TAG << ": Set CBR mode with Bitrate: " << (m_config.bitrate / 1000) << " Kbps" << std::endl;
            break;
    }
    
    // Create VENC channel
    s32Ret = CVI_VENC_CreateChn(m_vencChn, &stVencChnAttr);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << TAG << ": CVI_VENC_CreateChn failed with " << s32Ret << std::endl;
        return false;
    }
    
    // Set additional rate control parameters for better quality
    VENC_RC_PARAM_S stRcParam;
    memset(&stRcParam, 0, sizeof(VENC_RC_PARAM_S));
    
    s32Ret = CVI_VENC_GetRcParam(m_vencChn, &stRcParam);
    if (s32Ret == CVI_SUCCESS) {
        // Set initial QP for I-frames
        stRcParam.s32FirstFrameStartQp = m_config.qpInit;
        
        // Set QP ranges using threshold arrays
        stRcParam.u32ThrdI[0] = m_config.qpMin;  // Min I-frame QP threshold
        stRcParam.u32ThrdI[1] = m_config.qpMax;  // Max I-frame QP threshold
        stRcParam.u32ThrdP[0] = m_config.qpMin;  // Min P-frame QP threshold
        stRcParam.u32ThrdP[1] = m_config.qpMax;  // Max P-frame QP threshold
        
        // Print the QP settings for debugging
        std::cout << TAG << ": Setting QP parameters - Init: " << m_config.qpInit
                  << ", Min: " << m_config.qpMin << ", Max: " << m_config.qpMax << std::endl;
        
        // Apply RC parameters
        s32Ret = CVI_VENC_SetRcParam(m_vencChn, &stRcParam);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << TAG << ": CVI_VENC_SetRcParam failed with " << s32Ret << std::endl;
            // Non-fatal, continue
        }
    }
    
    // Start VENC channel with unlimited frame reception
    VENC_RECV_PIC_PARAM_S stRecvParam;
    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;  // Unlimited
    
    s32Ret = CVI_VENC_StartRecvFrame(m_vencChn, &stRecvParam);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << TAG << ": CVI_VENC_StartRecvFrame failed with " << s32Ret << std::endl;
        CVI_VENC_DestroyChn(m_vencChn);
        m_vencChn = -1;
        return false;
    }
    
    // Verify the encoder settings
    VENC_CHN_ATTR_S stActualAttr;
    memset(&stActualAttr, 0, sizeof(VENC_CHN_ATTR_S));
    s32Ret = CVI_VENC_GetChnAttr(m_vencChn, &stActualAttr);
    if (s32Ret == CVI_SUCCESS) {
        std::cout << TAG << ": Encoder settings verification:" << std::endl;
        std::cout << TAG << ":   - Actual RC mode: " << stActualAttr.stRcAttr.enRcMode << std::endl;
        
        switch (stActualAttr.stRcAttr.enRcMode) {
            case VENC_RC_MODE_H264FIXQP:
                std::cout << TAG << ":   - FIXQP: I=" << stActualAttr.stRcAttr.stH264FixQp.u32IQp
                          << ", P=" << stActualAttr.stRcAttr.stH264FixQp.u32PQp << std::endl;
                break;
            case VENC_RC_MODE_H264CBR:
                std::cout << TAG << ":   - CBR bitrate: " << stActualAttr.stRcAttr.stH264Cbr.u32BitRate << " Kbps" << std::endl;
                break;
            case VENC_RC_MODE_H264VBR:
                std::cout << TAG << ":   - VBR max bitrate: " << stActualAttr.stRcAttr.stH264Vbr.u32MaxBitRate << " Kbps" << std::endl;
                break;
            case VENC_RC_MODE_H264AVBR:
                std::cout << TAG << ":   - AVBR max bitrate: " << stActualAttr.stRcAttr.stH264AVbr.u32MaxBitRate << " Kbps" << std::endl;
                break;
            default:
                std::cout << TAG << ":   - Unknown RC mode!" << std::endl;
        }
    }
    
    std::cout << TAG << ": H264 encoder initialized on channel " << m_vencChn << std::endl;
    return true;
}

// Initialize RTSP server
bool CviH264Streamer::initRtsp() {
    CVI_S32 s32Ret;
    
    // Configure RTSP context
    m_rtspCtx.port = m_config.port;
    m_rtspCtx.session_cnt = 1;  // Single session for now
    m_rtspCtx.VencChn[0] = m_vencChn;  // Use the configured VENC channel
    
    // Set up session attributes
    m_rtspCtx.SessionAttr[0].video.codec = RTSP_VIDEO_H264;
    m_rtspCtx.SessionAttr[0].video.bitrate = m_config.bitrate / 1000;  // In Kbps
    
    // Create RTSP server
    CVI_RTSP_CONFIG config = {0};
    config.port = m_rtspCtx.port;
    
    s32Ret = CVI_RTSP_Create(&m_rtspCtx.pstServerCtx, &config);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << TAG << ": CVI_RTSP_Create failed with " << s32Ret << std::endl;
        return false;
    }
    
    // Initialize mutex for thread safety
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m_rtspCtx.mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    
    // Start RTSP server
    s32Ret = CVI_RTSP_Start(m_rtspCtx.pstServerCtx);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << TAG << ": CVI_RTSP_Start failed with " << s32Ret << std::endl;
        CVI_RTSP_Destroy(&m_rtspCtx.pstServerCtx);
        pthread_mutex_destroy(&m_rtspCtx.mutex);
        return false;
    }
    
    // Create RTSP session
    pthread_mutex_lock(&m_rtspCtx.mutex);
    
    // Set session name
    memset(m_rtspCtx.SessionAttr[0].name, 0, sizeof(m_rtspCtx.SessionAttr[0].name));
    snprintf(m_rtspCtx.SessionAttr[0].name, sizeof(m_rtspCtx.SessionAttr[0].name) - 1, 
             "%s", m_config.streamName.c_str());
    
    // Enable reuse of first source for multiple clients
    m_rtspCtx.SessionAttr[0].reuseFirstSource = 1;
    
    // Create the session
    s32Ret = CVI_RTSP_CreateSession(m_rtspCtx.pstServerCtx, &m_rtspCtx.SessionAttr[0], 
                                    &m_rtspCtx.pstSession[0]);
    if (s32Ret != CVI_SUCCESS || m_rtspCtx.pstSession[0] == NULL) {
        pthread_mutex_unlock(&m_rtspCtx.mutex);
        std::cerr << TAG << ": CVI_RTSP_CreateSession failed with " << s32Ret << std::endl;
        CVI_RTSP_Stop(m_rtspCtx.pstServerCtx);
        CVI_RTSP_Destroy(&m_rtspCtx.pstServerCtx);
        pthread_mutex_destroy(&m_rtspCtx.mutex);
        return false;
    }
    
    // Mark session as started
    m_rtspCtx.bStart[0] = CVI_TRUE;
    
    // Set up connection listeners with client counting
    m_rtspCtx.listener.onConnect = [](const char *ip, CVI_VOID *arg) {
        CviH264Streamer *pThis = static_cast<CviH264Streamer*>(arg);
        if (pThis) {
            pThis->m_clientCount++;
            std::cout << TAG << ": RTSP client connected from " << ip 
                      << " (total: " << pThis->m_clientCount.load() << ")" << std::endl;
            
            pThis->forceIFrame();
        }
    };
    m_rtspCtx.listener.argConn = this;
    
    m_rtspCtx.listener.onDisconnect = [](const char *ip, CVI_VOID *arg) {
        CviH264Streamer *pThis = static_cast<CviH264Streamer*>(arg);
        if (pThis && pThis->m_clientCount.load() > 0) {
            pThis->m_clientCount--;
            std::cout << TAG << ": RTSP client disconnected: " << ip 
                      << " (remaining: " << pThis->m_clientCount.load() << ")" << std::endl;
        }
    };
    m_rtspCtx.listener.argDisconn = this;
    
    // Register the listeners
    CVI_RTSP_SetListener(m_rtspCtx.pstServerCtx, &m_rtspCtx.listener);
    
    pthread_mutex_unlock(&m_rtspCtx.mutex);
    
    std::cout << TAG << ": RTSP server initialized on port " << m_config.port << std::endl;
    
    return true;
}

// Create YUV frame from OpenCV image
bool CviH264Streamer::createYuvFrame(const cv::Mat& frame, VIDEO_FRAME_INFO_S* pstFrame) {
    if (!pstFrame) {
        std::cerr << TAG << ": Invalid frame pointer" << std::endl;
        return false;
    }
    
    // Use static buffers to avoid repeated memory allocations
    static cv::Mat processedFrame;
    static cv::Mat yuv;
    static bool yuvNeedsInit = true;
    static uint64_t lastFrameSize = 0;
    
    // Get a reference to the input frame with proper dimensions
    const cv::Mat& frameToProcess = (frame.cols == m_config.width && frame.rows == m_config.height) 
        ? frame  // Use original frame if dimensions match
        : [&]() {
            // Resize only if needed
            if (processedFrame.empty() || processedFrame.cols != m_config.width || processedFrame.rows != m_config.height) {
                processedFrame.create(m_config.height, m_config.width, CV_8UC3);
            }
            cv::resize(frame, processedFrame, cv::Size(m_config.width, m_config.height), 0, 0, cv::INTER_LINEAR);
            return processedFrame;
        }();
    
    // Initialize or resize YUV buffer if needed
    uint64_t currentFrameSize = m_config.width * m_config.height * 3 / 2;
    if (yuvNeedsInit || yuv.empty() || lastFrameSize != currentFrameSize) {
        yuv.create(m_config.height*3/2, m_config.width, CV_8UC1);
        yuvNeedsInit = false;
        lastFrameSize = currentFrameSize;
    }
    
    // Calculate required size for YUV frame with proper padding
    CVI_U32 y_size = m_config.width * m_config.height;
    CVI_U32 uv_size = y_size / 2;  // For YUV420
    CVI_U32 u32Size = y_size + uv_size;
    
    // Align the size to a safe boundary
    u32Size = ALIGN_UP(u32Size, 1024);
    
    // Get a block from VB pool - try the fast path first without sleep
    VB_BLK VbBlk = CVI_VB_GetBlock(VB_INVALID_POOLID, u32Size);
    if (VbBlk == VB_INVALID_HANDLE) {
        // Try again after a short delay - reduce sleep time
        usleep(1000);  // Reduced from 5ms to 1ms
        VbBlk = CVI_VB_GetBlock(VB_INVALID_POOLID, u32Size);
        if (VbBlk == VB_INVALID_HANDLE) {
            std::cerr << TAG << ": Failed to get VB block of size " << u32Size << ", retrying..." << std::endl;
            usleep(5000);  // Wait longer on second retry
            VbBlk = CVI_VB_GetBlock(VB_INVALID_POOLID, u32Size);
            if (VbBlk == VB_INVALID_HANDLE) {
                std::cerr << TAG << ": Failed to get VB block after retries" << std::endl;
                return false;
            }
        }
    }
    
    // Get physical address of the block
    CVI_U64 u64PhyAddr = CVI_VB_Handle2PhysAddr(VbBlk);
    if (u64PhyAddr == 0) {
        std::cerr << TAG << ": Failed to get physical address" << std::endl;
        CVI_VB_ReleaseBlock(VbBlk);
        return false;
    }
    
    // Get virtual address for CPU access
    CVI_VOID *pVirAddr = CVI_SYS_Mmap(u64PhyAddr, u32Size);
    if (pVirAddr == NULL) {
        std::cerr << TAG << ": Failed to get virtual address" << std::endl;
        CVI_VB_ReleaseBlock(VbBlk);
        return false;
    }
    
    // Convert BGR to YUV I420 with optimized approach based on channels
    if (frameToProcess.channels() == 3) {
        // Fast BGR to YUV I420 conversion directly to our buffer
        // This is the most common case, so optimize it first
        cv::cvtColor(frameToProcess, yuv, cv::COLOR_BGR2YUV_I420);
        
        // Copy YUV data directly to mapped memory
        memcpy(pVirAddr, yuv.data, yuv.total());
    } else if (frameToProcess.channels() == 1) {
        // Grayscale optimization - directly copy Y plane and fill U/V
        unsigned char* virY = (unsigned char*)pVirAddr;
        unsigned char* virU = virY + y_size;
        unsigned char* virV = virU + (y_size/4);
        
        // Copy Y plane directly from grayscale frame to avoid extra buffer
        memcpy(virY, frameToProcess.data, y_size);
        
        // Fill U and V planes with 128 (neutral) - fast memory fill
        memset(virU, 128, y_size/4);
        memset(virV, 128, y_size/4);
    } else {
        std::cerr << TAG << ": Unsupported image format: " << frameToProcess.channels() << " channels" << std::endl;
        CVI_SYS_Munmap(pVirAddr, u32Size);
        CVI_VB_ReleaseBlock(VbBlk);
        return false;
    }
    
    // Initialize frame info structure
    memset(pstFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
    
    // Fill frame info
    pstFrame->stVFrame.u32Width = m_config.width;
    pstFrame->stVFrame.u32Height = m_config.height;
    pstFrame->stVFrame.enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_420;
    pstFrame->stVFrame.enCompressMode = COMPRESS_MODE_NONE;
    pstFrame->stVFrame.enVideoFormat = VIDEO_FORMAT_LINEAR;
    
    // Set stride (width with alignment as required)
    pstFrame->stVFrame.u32Stride[0] = m_config.width;
    pstFrame->stVFrame.u32Stride[1] = m_config.width / 2;
    pstFrame->stVFrame.u32Stride[2] = m_config.width / 2;
    
    // Set physical addresses for Y, U, V planes
    pstFrame->stVFrame.u64PhyAddr[0] = u64PhyAddr;
    pstFrame->stVFrame.u64PhyAddr[1] = u64PhyAddr + y_size;
    pstFrame->stVFrame.u64PhyAddr[2] = u64PhyAddr + y_size + uv_size / 2;
    
    // Save the VB block handle for later release
    pstFrame->stVFrame.pPrivateData = (void*)(uintptr_t)VbBlk;
    
    // Unmap the buffer as we're done with CPU access
    CVI_SYS_Munmap(pVirAddr, u32Size);
    
    return true;
}

// Send encoded data to RTSP
bool CviH264Streamer::sendEncodedDataToRtsp(VENC_STREAM_S* pstStream) {
    if (!pstStream || !m_initialized) {
        return false;
    }
    
    // Check if we have any packets to send
    if (pstStream->u32PackCount == 0) {
        // Not an error, just no data
        return true;
    }
    
    // Calculate bitrate statistics
    static uint64_t totalBytes = 0;
    static uint64_t lastBitrateCheck = 0;
    static int framesSinceLastCheck = 0;
    static int iFramesSinceLastCheck = 0;
    
    uint64_t currentTime = getCurrentTimeMs();
    uint64_t packetSize = 0;
    bool isIFrame = false;
    
    // Create RTSP data structure to hold the encoded data
    CVI_RTSP_DATA rtspData;
    memset(&rtspData, 0, sizeof(CVI_RTSP_DATA));
    
    // Copy encoded data information
    rtspData.blockCnt = pstStream->u32PackCount;
    
    // Setup data pointers for each packet
    for (CVI_U32 i = 0; i < pstStream->u32PackCount; i++) {
        VENC_PACK_S* pPack = &pstStream->pstPack[i];
        rtspData.dataPtr[i] = pPack->pu8Addr + pPack->u32Offset;
        rtspData.dataLen[i] = pPack->u32Len - pPack->u32Offset;
        
        packetSize += rtspData.dataLen[i];
        
        // Debug log for I-frames
        if (pPack->DataType.enH264EType == H264E_NALU_IDRSLICE) {
            isIFrame = true;
            std::cout << TAG << ": Sending I-frame, size: " << rtspData.dataLen[i] << " bytes. Frame count: " << m_frameCount.load() << std::endl;
            // Update last I-frame time
            m_lastIFrameTime = getCurrentTimeMs();
            
            // Update global I-frame count
            m_totalIFrames++;
            iFramesSinceLastCheck++;
        }
    }
    
    // Update bitrate statistics
    totalBytes += packetSize;
    framesSinceLastCheck++;
    m_totalBytes += packetSize;
    m_totalFrames++;
    
    // Report bitrate every REPORT_BITRATE_INTERVAL_MS
    if (lastBitrateCheck == 0 || (currentTime - lastBitrateCheck) >= REPORT_BITRATE_INTERVAL_MS) {
        if (lastBitrateCheck > 0) {
            double elapsedSec = (currentTime - lastBitrateCheck) / 1000.0;
            double bitrate = (totalBytes * 8.0) / elapsedSec; // bits per second
            
            // Get encoder status for QP information
            VENC_CHN_STATUS_S stStat;
            CVI_S32 s32Ret = CVI_VENC_QueryStatus(m_vencChn, &stStat);
            
            std::cout << TAG << ": STATS - Actual bitrate: " << std::fixed << std::setprecision(2) 
                      << (bitrate / 1000000.0) << " Mbps, Frame size avg: " 
                      << (totalBytes / framesSinceLastCheck) << " bytes";
                      
            if (s32Ret == CVI_SUCCESS) {
                /*
                typedef struct _VENC_STREAM_INFO_S {
                    H265E_REF_TYPE_E enRefType; //Type of encoded frames in advanced frame skipping reference mode 

                    CVI_U32 u32PicBytesNum; // the coded picture stream byte number 
                    CVI_U32 u32PicCnt; //Number of times that channel attributes or parameters (including RC parameters) are set 
                    CVI_U32 u32StartQp; //the start Qp of encoded frames
                    CVI_U32 u32MeanQp; //the mean Qp of encoded frames
                    CVI_BOOL bPSkip;

                    CVI_U32 u32ResidualBitNum; // residual
                    CVI_U32 u32HeadBitNum; // head information
                    CVI_U32 u32MadiVal; // madi
                    CVI_U32 u32MadpVal; // madp
                    CVI_U32 u32MseSum; // Sum of MSE value 
                    CVI_U32 u32MseLcuCnt; // Sum of LCU number 
                    double dPSNRVal; // PSNR
                } VENC_STREAM_INFO_S;
                */

                std::cout << ", Avg QP: " << stStat.stVencStrmInfo.u32MeanQp
                          << ", Start QP: " << stStat.stVencStrmInfo.u32StartQp;
            }
            
            std::cout << ", I-frames: " << iFramesSinceLastCheck
                      << std::endl;
        }
        
        // Reset counters
        totalBytes = 0;
        lastBitrateCheck = currentTime;
        framesSinceLastCheck = 0;
        iFramesSinceLastCheck = 0;
    }
    
    // Lock RTSP context for thread safety
    pthread_mutex_lock(&m_rtspCtx.mutex);
    
    bool success = false;
    
    // Check if session is active
    if (m_rtspCtx.bStart[0] && m_rtspCtx.pstServerCtx && m_rtspCtx.pstSession[0]) {
        // Write the frame to the RTSP session
        CVI_S32 s32Ret = CVI_RTSP_WriteFrame(m_rtspCtx.pstServerCtx, 
                                             m_rtspCtx.pstSession[0]->video, 
                                             &rtspData);
        
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << TAG << ": CVI_RTSP_WriteFrame failed with " << s32Ret << std::endl;
            m_errorCount++;
        } else {
            success = true;
        }
    } else {
        std::cerr << TAG << ": RTSP session not ready" << std::endl;
    }
    
    pthread_mutex_unlock(&m_rtspCtx.mutex);
    
    return success;
}

// Thread function for encoding frames
void CviH264Streamer::encodingThreadFunc() {
    std::cout << TAG << ": Encoding thread started" << std::endl;
    
    // For adaptive processing - removed frame skipping logic
    int queueSizeHighWatermark = 0;
    
    while (m_threadRunning) {
        cv::Mat frame;
        bool hasFrame = false;
        int queueSize = 0;
        
        // Get frame from queue if available
        {
            std::unique_lock<std::mutex> lock(m_frameMutex);
            
            // Wait for a frame or until shutdown with shorter timeout when busy
            m_frameCondition.wait_for(lock, std::chrono::milliseconds(50), 
                [this] { return !m_frameQueue.empty() || !m_threadRunning; });
            
            if (!m_threadRunning) {
                break;  // Exit if shutting down
            }
            
            queueSize = m_frameQueue.size();
            if (queueSize > queueSizeHighWatermark) {
                queueSizeHighWatermark = queueSize;
                // Only log when we reach new high watermarks
                if (queueSize > m_maxQueueSize / 2) {
                    std::cout << TAG << ": Queue size high watermark: " << queueSize 
                              << " / " << m_maxQueueSize << std::endl;
                }
            }
            
            if (!m_frameQueue.empty()) {
                // Use move semantics to avoid copying the frame data
                frame = std::move(m_frameQueue.front());
                m_frameQueue.pop();
                hasFrame = true;
                
                // If queue has space, notify sendFrame() in case it's blocking
                if (m_frameQueue.size() < m_maxQueueSize) {
                    lock.unlock();  // Unlock before notification for better concurrency
                    m_frameCondition.notify_all();
                }
            }
        }
        
        // Process the frame if we got one
        if (hasFrame) {
            // Process the frame - no clone needed since we moved it from the queue
            if (!processFrame(frame)) {
                std::cerr << TAG << ": Failed to process frame" << std::endl;
            }
        }
    }
    
    std::cout << TAG << ": Encoding thread stopped" << std::endl;
}

// Send a frame to be encoded and streamed (now non-blocking)
bool CviH264Streamer::sendFrame(const cv::Mat& frame, bool blocking) {
    if (!m_initialized || !m_running.load()) {
        return false;
    }
    
    // Check for valid frame
    if (frame.empty()) {
        std::cerr << TAG << ": Empty frame received" << std::endl;
        return false;
    }
    
    // Add frame to the encoding queue
    {
        std::unique_lock<std::mutex> lock(m_frameMutex);
        
        // If queue is full and blocking mode is enabled, wait for space
        if (blocking && m_frameQueue.size() >= m_maxQueueSize) {
            // Only log occasionally to avoid spamming
            static uint64_t lastLogTime = 0;
            uint64_t currentTime = getCurrentTimeMs();
            if (currentTime - lastLogTime > 5000) { // Log at most every 5 seconds
                std::cout << TAG << ": Queue full, waiting for space..." << std::endl;
                lastLogTime = currentTime;
            }
            
            // Wait for queue space with a timeout to prevent deadlock
            auto waitResult = m_frameCondition.wait_for(lock, 
                std::chrono::seconds(5), // 5 second timeout
                [this] { return m_frameQueue.size() < m_maxQueueSize || !m_running.load(); });
            
            // Check if we're shutting down
            if (!m_running.load()) {
                return false;
            }
            
            // Check if timeout occurred
            if (!waitResult) {
                std::cerr << TAG << ": Timeout waiting for queue space" << std::endl;
                return false;
            }
        }
        // Non-blocking mode: if queue is full, drop the oldest frame
        else if (m_frameQueue.size() >= m_maxQueueSize) {
            m_frameQueue.pop();
            // Count this as an error
            m_errorCount++;
            
            // Log frame drops only occasionally
            static uint64_t lastDropLogTime = 0;
            static int dropCount = 0;
            dropCount++;
            
            uint64_t currentTime = getCurrentTimeMs();
            if (currentTime - lastDropLogTime > 5000) { // Log at most every 5 seconds
                std::cerr << TAG << ": Dropped " << dropCount << " frames in the last 5 seconds. Queue size: " 
                          << m_frameQueue.size() << std::endl;
                lastDropLogTime = currentTime;
                dropCount = 0;
            }
        }
        
        // Add the new frame without cloning
        m_frameQueue.push(frame);  // No need to clone since OpenCV's Mat uses reference counting
    }
    
    // Signal the encoding thread
    m_frameCondition.notify_one();
    return true;
}

// Process a single frame (called from encoding thread)
bool CviH264Streamer::processFrame(const cv::Mat& frame) {
    try {
        // Increment frame counter
        uint64_t frameNum = m_frameCount.fetch_add(1);
        
        // Check if we need to force an I-frame, but only log on actual requests
        uint64_t currentTime = getCurrentTimeMs();
        static uint64_t iFrameInterval = 5000; // Start with 5 seconds
        
        bool forceIFrameNow = false;
        if (m_lastIFrameTime == 0 || (currentTime - m_lastIFrameTime) > iFrameInterval) {
            // Calculate dynamic I-frame interval based on performance
            // If we're dropping frames, increase the I-frame interval
            if (frameNum > 100) {
                if (m_errorCount.load() > 0) {
                    // More errors = longer I-frame interval to reduce overhead
                    iFrameInterval = std::min(iFrameInterval + 1000, (uint64_t)10000); // Cap at 10 seconds
                } else {
                    // No errors = we can afford more I-frames
                    iFrameInterval = std::max(iFrameInterval - 500, (uint64_t)2000); // Minimum 2 seconds
                }
            }
            
            forceIFrameNow = true;
            std::cout << TAG << ": Forcing I-frame... Frame count: " << frameNum << std::endl;
            forceIFrame();
            m_lastIFrameTime = currentTime;
        }
        
        // Create a YUV frame from the input OpenCV image
        VIDEO_FRAME_INFO_S stFrame;
        if (!createYuvFrame(frame, &stFrame)) {
            std::cerr << TAG << ": Failed to create YUV frame" << std::endl;
            return false;
        }
        
        // Send the frame to the encoder with a shorter timeout
        CVI_S32 s32Ret = CVI_VENC_SendFrame(m_vencChn, &stFrame, 500);  // 500ms timeout (reduced from 1000ms)
        if (s32Ret != CVI_SUCCESS) {
            // Only log errors occasionally
            if (frameNum % 20 == 0) {
                std::cerr << TAG << ": CVI_VENC_SendFrame failed with " << s32Ret << std::endl;
            }
            // Always release VB block
            CVI_VB_ReleaseBlock((VB_BLK)(uintptr_t)stFrame.stVFrame.pPrivateData);
            return false;
        }
        
        // Release the VB block now that we've sent it to the encoder
        CVI_VB_ReleaseBlock((VB_BLK)(uintptr_t)stFrame.stVFrame.pPrivateData);
        
        // Check VENC status to get the number of encoded packets ready
        VENC_CHN_STATUS_S stStat;
        s32Ret = CVI_VENC_QueryStatus(m_vencChn, &stStat);
        if (s32Ret != CVI_SUCCESS) {
            // Only log errors occasionally
            if (frameNum % 20 == 0) {
                std::cerr << TAG << ": CVI_VENC_QueryStatus failed with " << s32Ret << std::endl;
            }
            return false;
        }
        
        if (stStat.u32CurPacks == 0) {
            // No packets available yet, not an error
            return true;
        }
        
        // Allocate memory for stream packs - use stack allocation for small counts
        VENC_STREAM_S stStream;
        memset(&stStream, 0, sizeof(VENC_STREAM_S));
        
        // Use stack allocation for small packet counts to avoid malloc/free overhead
        const int MAX_STACK_PACKS = 8;
        VENC_PACK_S stackPacks[MAX_STACK_PACKS];
        
        if (stStat.u32CurPacks <= MAX_STACK_PACKS) {
            // Use stack-allocated buffer for small packet counts
            stStream.pstPack = stackPacks;
        } else {
            // Use heap for larger packet counts
            stStream.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
            if (stStream.pstPack == NULL) {
                std::cerr << TAG << ": Failed to allocate memory for stream packs" << std::endl;
                return false;
            }
        }
        
        // Get the encoded stream with shorter timeout
        s32Ret = CVI_VENC_GetStream(m_vencChn, &stStream, 300);  // 300ms timeout (reduced from 1000ms)
        if (s32Ret != CVI_SUCCESS) {
            // Only log errors occasionally
            if (frameNum % 20 == 0) {
                std::cerr << TAG << ": CVI_VENC_GetStream failed with " << s32Ret << std::endl;
            }
            if (stStat.u32CurPacks > MAX_STACK_PACKS) {
                free(stStream.pstPack);
            }
            return false;
        }
        
        // Send the encoded stream to RTSP clients
        bool result = sendEncodedDataToRtsp(&stStream);
        
        // Release the stream
        s32Ret = CVI_VENC_ReleaseStream(m_vencChn, &stStream);
        if (s32Ret != CVI_SUCCESS && frameNum % 20 == 0) {
            std::cerr << TAG << ": CVI_VENC_ReleaseStream failed with " << s32Ret << std::endl;
            // Non-fatal, continue
        }
        
        // Free allocated memory if we used heap
        if (stStat.u32CurPacks > MAX_STACK_PACKS) {
            free(stStream.pstPack);
        }
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << TAG << ": Exception in processFrame: " << e.what() << std::endl;
        return false;
    }
}

// Release all resources
void CviH264Streamer::cleanup() {
    if (!m_running.load()) {
        return;  // Already cleaned up
    }
    
    std::cout << TAG << ": Cleaning up resources..." << std::endl;
    
    // Set running flag to false first to stop ongoing operations
    m_running.store(false);
    
    // Stop and join the encoding thread
    m_threadRunning = false;
    m_frameCondition.notify_all();  // Wake up thread if waiting
    
    if (m_encodingThread.joinable()) {
        m_encodingThread.join();
    }
    
    // Clear frame queue
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        while (!m_frameQueue.empty()) {
            m_frameQueue.pop();
        }
    }
    
    // Clean up RTSP resources
    if (m_rtspCtx.pstServerCtx) {
        // Stop server first
        CVI_S32 s32Ret = CVI_RTSP_Stop(m_rtspCtx.pstServerCtx);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << TAG << ": CVI_RTSP_Stop failed with " << s32Ret << std::endl;
        }
        
        // Destroy sessions
        pthread_mutex_lock(&m_rtspCtx.mutex);
        for (CVI_S32 i = 0; i < m_rtspCtx.session_cnt; i++) {
            if (m_rtspCtx.bStart[i] && m_rtspCtx.pstSession[i]) {
                s32Ret = CVI_RTSP_DestroySession(m_rtspCtx.pstServerCtx, m_rtspCtx.pstSession[i]);
                if (s32Ret != CVI_SUCCESS) {
                    std::cerr << TAG << ": CVI_RTSP_DestroySession failed with " << s32Ret << std::endl;
                }
                m_rtspCtx.bStart[i] = CVI_FALSE;
                m_rtspCtx.pstSession[i] = NULL;
            }
        }
        pthread_mutex_unlock(&m_rtspCtx.mutex);
        
        // Destroy server
        s32Ret = CVI_RTSP_Destroy(&m_rtspCtx.pstServerCtx);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << TAG << ": CVI_RTSP_Destroy failed with " << s32Ret << std::endl;
        }
        
        // Destroy mutex
        pthread_mutex_destroy(&m_rtspCtx.mutex);
    }
    
    // Clean up VENC resources
    if (m_vencChn >= 0) {
        // Stop receiving frames
        CVI_S32 s32Ret = CVI_VENC_StopRecvFrame(m_vencChn);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << TAG << ": CVI_VENC_StopRecvFrame failed with " << s32Ret << std::endl;
        }
        
        // Short delay to ensure any pending operations complete
        usleep(100000);  // 100ms
        
        // Destroy channel
        s32Ret = CVI_VENC_DestroyChn(m_vencChn);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << TAG << ": CVI_VENC_DestroyChn failed with " << s32Ret << std::endl;
        }
        
        m_vencChn = -1;
    }
    
    cvi_system_Sys_DeInit();
    deinitVideo(false);

    
    // Reset state variables
    m_initialized = false;
    m_clientCount.store(0);
    
    // Log statistics
    uint64_t totalTime = getCurrentTimeMs() - m_startTime;
    std::cout << TAG << ": Cleanup completed. Statistics:" << std::endl;
    std::cout << TAG << ":   - Total frames: " << m_frameCount.load() << std::endl;
    std::cout << TAG << ":   - Errors: " << m_errorCount.load() << std::endl;
    if (totalTime > 0 && m_frameCount.load() > 0) {
        double avgFps = (m_frameCount.load() * 1000.0) / totalTime;
        std::cout << TAG << ":   - Average FPS: " << avgFps << std::endl;
    }
}

// Get the RTSP URL for this stream
std::string CviH264Streamer::getStreamUrl() const {
    // Return the list of available URLs
    return getAvailableIpAddresses();
}

// Check if initialized
bool CviH264Streamer::isInitialized() const {
    return m_initialized;
} 