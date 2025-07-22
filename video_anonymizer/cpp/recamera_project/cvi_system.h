// This file includes all the needed headers of the sophgo library from the sscma-example-sg200x repository.
// All this code is licensed under the Apache License 2.0 included in the sscma-example-sg200x repository: https://github.com/Seeed-Studio/sscma-example-sg200x/blob/main/LICENSE
// Some minor modifications have been made to the code to adapt it to the requirements of this project.


#pragma once

#define DEF_DEBUG_LEVEL            	LEVEL_INFO

#include <cstdint>

#include <linux/cvi_comm_sys.h>
#include <cvi_type.h>
#include <cvi_vpss.h>
#include <cvi_sys.h>

#include <opencv2/opencv.hpp>

#include "app_ipcam_comm.h"

typedef enum {
    VIDEO_FORMAT_RGB888 = 0, // no need venc
    VIDEO_FORMAT_NV21, // no need venc
    VIDEO_FORMAT_JPEG,
    VIDEO_FORMAT_H264,
    VIDEO_FORMAT_H265,

    VIDEO_FORMAT_COUNT
} video_format_t;

typedef enum {
    VIDEO_CH0 = 0,
    VIDEO_CH1,
    VIDEO_CH2,

    VIDEO_CH_MAX
} video_ch_index_t;

typedef struct {
    video_format_t format;
    uint32_t width;
    uint32_t height;
    uint8_t fps;
} video_ch_param_t;

int initVideo(bool use_venc);
int deinitVideo(bool stop_venc);
int startVideo(bool start_venc);
int setupVideo(video_ch_index_t ch, const video_ch_param_t* param, bool setup_venc);

typedef int (*pfpDataConsumes)(void *pData, void *pCtx, void *pUserData);
int registerVideoFrameHandler(video_ch_index_t ch, int index, pfpDataConsumes handler, void* pUserData);

bool getVideoFrame(video_ch_index_t ch, cv::Mat &frame, int timeout_ms);


int cvi_system_setVbPool(video_ch_index_t ch, const video_ch_param_t* param, uint32_t u32BlkCnt = 2);

int cvi_system_Sys_Init(void);
int cvi_system_Sys_DeInit(void);
