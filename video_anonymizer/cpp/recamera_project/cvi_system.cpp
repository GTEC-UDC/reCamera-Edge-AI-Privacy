// This file includes all the needed code of the sophgo library from the sscma-example-sg200x repository.
// All this code is licensed under the Apache License 2.0 included in the sscma-example-sg200x repository: https://github.com/Seeed-Studio/sscma-example-sg200x/blob/main/LICENSE
// Some minor modifications have been made to the code to adapt it to the requirements of this project.

#include "cvi_system.h"

#include <iostream>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stddef.h>
#include <atomic>
#include <chrono>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <linux/cvi_comm_sys.h>
#include <cvi_ae.h>
#include <cvi_awb.h>
#include <cvi_bin.h>

#include <sys/prctl.h>
#include <stdlib.h>
#include <errno.h>

#include <cvi_type.h>
#include <cvi_vpss.h>

#include "cvi_vb.h"
#include "cvi_buffer.h"

#include "app_ipcam_paramparse.h"
#include "app_ipcam_ll.h"
#include "app_ipcam_comm.h"


// app_ipcam_comm.c




unsigned int GetCurTimeInMsec(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) < 0)
    {
        return 0;
    }

    return tv.tv_sec * 1000 + tv.tv_usec/1000;
}

// app_ipcam_ll.c



#define LL_DATA_CACHE_DEPTH_MAX     30

#define LL_INIT(N) ((N)->next = (N)->prev = (N))

#define LL_HEAD(H) struct llhead H = { &H, &H }

#define LL_ENTRY(P,T,N) ((T *)((char *)(P) - offsetof(T, N)))

#define LL_ADD(H, N) do {       \
    ((H)->next)->prev = (N);    \
    (N)->next = ((H)->next);    \
    (N)->prev = (H);            \
    (H)->next = (N);            \
} while (0)

#define LL_TAIL(H, N) do {      \
    ((H)->prev)->next = (N);    \
    (N)->prev = ((H)->prev);    \
    (N)->next = (H);            \
    (H)->prev = (N);            \
} while (0)

#define LL_DEL(N) do {                  \
    ((N)->next)->prev = ((N)->prev);    \
    ((N)->prev)->next = ((N)->next);    \
    LL_INIT(N);                         \
} while (0)

#define LL_EMPTY(N) ((N)->next == (N))

#define LL_FOREACH(H,N) for (N = (H)->next; N != (H); N = (N)->next)

#define LL_FOREACH_SAFE(H,N,T)  for (N = (H)->next, T = (N)->next; N != (H); N = (T), T = (N)->next)

#define LL_DATA_NODE_DEL(pNode) do {                \
    if (pNode != NULL) {                            \
        ((pNode)->next)->prev = ((pNode)->prev);    \
        ((pNode)->prev)->next = ((pNode)->next);    \
    }                                               \
} while(0)

#define container_of(ptr, type, member) ({          \
    const typeof(((type *)0)->member) *mptr = ptr;  \
    (type *)((char *)mptr-offsetof(type, member));  \
})

#define LINK_LIST_DATA_POP_FRONT(PopPoint, Head, member) ({             \
    if(Head->next == Head) {                                            \
        PopPoint = NULL;                                                \
    } else {                                                            \
        PopPoint = container_of(Head->next, typeof(*PopPoint), member); \
        LL_DATA_NODE_DEL(Head->next);                                   \
    }                                                                   \
})

#define LINK_LIST_DATA_PUSH_TAIL(H, N) do { \
    ((H)->prev)->next = (N);                \
    (N)->prev = ((H)->prev);                \
    (N)->next = (H);                        \
    (H)->prev = (N);                        \
} while (0)

int app_ipcam_LList_Data_Pop(void **pData, void *pArgs)
{
    APP_DATA_CTX_S *pstDataCtx = (APP_DATA_CTX_S *)pArgs;

    APP_LINK_LIST_S *pHeadLink = &pstDataCtx->stHead.link;
    if (LL_EMPTY(pHeadLink)) {
        return -1;
    }

    APP_DATA_LL_S *pNodePop = NULL;

    pthread_mutex_lock(&pstDataCtx->mutex);
    LINK_LIST_DATA_POP_FRONT(pNodePop, pHeadLink, link);
    if(pNodePop) {
        pstDataCtx->LListDepth--;
    }
    pthread_mutex_unlock(&pstDataCtx->mutex);

    if(pNodePop) {
        *pData = pNodePop->pData;
        free(pNodePop);
    } else {
        return -1;
    }

    return 0;
}

int app_ipcam_LList_Data_Push(void *pData, void *pArgs)
{
    if (pData == NULL || pArgs == NULL) {
        printf("pData or pArgs is NULL!\n");
        return -1;
    }

    APP_DATA_CTX_S *pstDataCtx = (APP_DATA_CTX_S *)pArgs;
    APP_DATA_PARAM_S *pstDataParam = &pstDataCtx->stDataParam;
    APP_DATA_LL_S *pHead = &pstDataCtx->stHead;

    if (!pstDataCtx->bRunStatus) {
        printf("Link List Cache Not Running Now!!\n");
        return -1;
    }

    APP_DATA_LL_S *pNewNode = NULL;
    pNewNode = (APP_DATA_LL_S *)malloc(sizeof(APP_DATA_LL_S));
    if(pNewNode == NULL) {
        printf("pNewNode malloc failed!\n");
        return -1;
    }

    pNewNode->pData = NULL;

    if (pstDataParam->fpDataSave(&pNewNode->pData, pData) != 0) {
        free(pNewNode);
        printf("data save failded!\n");
        return -1;
    }

    if (pstDataCtx->LListDepth > LL_DATA_CACHE_DEPTH_MAX) {
        void *pDataDrop = NULL;
        printf("LL cache is full and drop data. (LList depth:%d > Max:%d) \n", pstDataCtx->LListDepth, LL_DATA_CACHE_DEPTH_MAX);
        if (app_ipcam_LList_Data_Pop(&pDataDrop, pArgs) != 0) {
            free(pNewNode);
            printf("LL data drop failded!\n");
            return -1;
        }
        if(pDataDrop) {
            if(pstDataParam->fpDataFree) {
                pstDataParam->fpDataFree(&pDataDrop);
            }
            pDataDrop = NULL;
        }
    }

    pthread_mutex_lock(&pstDataCtx->mutex);
    APP_LINK_LIST_S *pHeadLink = &pHead->link;
    APP_LINK_LIST_S *pNodeLink = &pNewNode->link;
    if (pHeadLink == NULL || pNodeLink == NULL) {
        printf(" pHeadLink or pNodeLink is NULL\n");
        pthread_mutex_unlock(&pstDataCtx->mutex);
        return -1;
    }

    LINK_LIST_DATA_PUSH_TAIL(pHeadLink, pNodeLink);

    pstDataCtx->LListDepth++;
    pthread_mutex_unlock(&pstDataCtx->mutex);

    return 0;

}

static void *Thread_LList_Data_Consume(void *pArgs)
{
    APP_DATA_CTX_S *pstDataCtx = (APP_DATA_CTX_S *)pArgs;
    APP_DATA_PARAM_S *pstDataParam = &pstDataCtx->stDataParam;

    void *pData = NULL;
    char TaskName[32] = {0};

    sprintf(TaskName, "DataConsume");
    prctl(PR_SET_NAME, TaskName, 0, 0, 0);
    while(pstDataCtx->bRunStatus) {
        if(app_ipcam_LList_Data_Pop(&pData, pArgs) == 0) {
            if(pData != NULL) {
                if(pstDataParam->fpDataHandle) {
                    pstDataParam->fpDataHandle(pData, pArgs);
                }
                if(pstDataParam->fpDataFree) {
                    pstDataParam->fpDataFree(&pData);
                }
                pData = NULL;
            }
        } else {
            usleep(5*1000);
        }
    }

    return NULL;
}

int app_ipcam_LList_Data_Init(void **pCtx, void *pParam)
{
    if ((pCtx == NULL) || (pParam == NULL)) {
        printf("pCtx or pParam is NULL\n");
        return -1;
    }

    int s32Ret = 0;
    APP_DATA_PARAM_S *pDataParam = (APP_DATA_PARAM_S *)pParam;
    APP_DATA_CTX_S *pDataCtx = (APP_DATA_CTX_S *)malloc(sizeof(APP_DATA_CTX_S));
    if (pDataCtx == NULL) {
        printf("pDataCtx is NULL\n");
        return -1;
    }

    pDataCtx->stDataParam = *pDataParam;
    pDataCtx->LListDepth = 0;
    pthread_mutex_init(&pDataCtx->mutex, NULL);

    /* init link list head */
    LL_INIT(&pDataCtx->stHead.link);

    pDataCtx->bRunStatus = true;
    s32Ret = pthread_create(&pDataCtx->pthread_id,
                            NULL,
                            Thread_LList_Data_Consume,
                            (void *)pDataCtx);
    if (s32Ret != 0) {
        pthread_mutex_destroy(&pDataCtx->mutex);
        goto EXIT;
    }

    *pCtx = pDataCtx;

    return s32Ret;

EXIT:
    if(pDataCtx != NULL) {
        free(pDataCtx);
    }
    pDataCtx->bRunStatus = false;

    return s32Ret;
}

int app_ipcam_LList_Data_DeInit(void * *pCtx)
{
    if ((pCtx == NULL) || (*pCtx == NULL)) {
        printf("pCtx or *pCtx is NULL\n");
        return -1;
    }

    APP_DATA_CTX_S *pstDataCtx = (APP_DATA_CTX_S *) *pCtx;
    APP_DATA_PARAM_S *pstDataParam = &pstDataCtx->stDataParam;

    void *pDataDrop = NULL;
    pstDataCtx->bRunStatus = false;

    pthread_join(pstDataCtx->pthread_id, NULL);

    while(app_ipcam_LList_Data_Pop(&pDataDrop, *pCtx) == 0) {
        if(pDataDrop) {
            if(pstDataParam->fpDataFree) {
                pstDataParam->fpDataFree(&pDataDrop);
            }
            pDataDrop = NULL;
        }
    }
    pstDataCtx->LListDepth = 0;
    pthread_mutex_destroy(&pstDataCtx->mutex);

    free(*pCtx);
    *pCtx = NULL;

    return 0;
}





// isp.c


/**************************************************************************
 *                              M A C R O S                               *
 **************************************************************************/
#ifdef SUPPORT_ISP_PQTOOL
#include <dlfcn.h>
#include "cvi_ispd2.h"

#define ISPD_LIBNAME "libcvi_ispd2.so"
#define ISPD_CONNECT_PORT 5566
/* 183x not support continuous RAW dump */
#ifndef ARCH_CV183X
#define RAW_DUMP_LIBNAME "libraw_dump.so"
#endif
#endif

/**************************************************************************
 *                           C O N S T A N T S                            *
 **************************************************************************/

/**************************************************************************
 *                          D A T A    T Y P E S                          *
 **************************************************************************/

/**************************************************************************
 *                         G L O B A L    D A T A                         *
 **************************************************************************/
static pthread_t g_IspPid[VI_MAX_DEV_NUM];

#ifdef SUPPORT_ISP_PQTOOL
static CVI_BOOL bISPDaemon = CVI_FALSE;
//static CVI_VOID *pISPDHandle = NULL;
#ifndef ARCH_CV183X
static CVI_BOOL bRawDump = CVI_FALSE;
static CVI_VOID *pRawDumpHandle = NULL;
#endif
#endif

// static APP_PARAM_VI_PM_DATA_S ViPmData[VI_MAX_DEV_NUM] = { 0 };

/**************************************************************************
 *                 E X T E R N A L    R E F E R E N C E S                 *
 **************************************************************************/

/**************************************************************************
 *               F U N C T I O N    D E C L A R A T I O N S               *
 **************************************************************************/
static CVI_S32 app_ipcam_ISP_ProcInfo_Open(CVI_U32 ProcLogLev)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    if (ProcLogLev == ISP_PROC_LOG_LEVEL_NONE) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "isp proc log not enable\n");
    } else {
        ISP_CTRL_PARAM_S setParam;
        memset(&setParam, 0, sizeof(ISP_CTRL_PARAM_S));

        setParam.u32ProcLevel = ProcLogLev;	// proc printf level (level =0,disable; =3,log max)
        setParam.u32ProcParam = 15;		// isp info frequency of collection (unit:frame; rang:(0,0xffffffff])
        setParam.u32AEStatIntvl = 1;	// AE info update frequency (unit:frame; rang:(0,0xffffffff])
        setParam.u32AWBStatIntvl = 6;	// AW info update frequency (unit:frame; rang:(0,0xffffffff])
        setParam.u32AFStatIntvl = 1;	// AF info update frequency (unit:frame; rang:(0,0xffffffff])
        setParam.u32UpdatePos = 0;		// Now, only support before sensor cfg; default 0
        setParam.u32IntTimeOut = 0;		// interrupt timeout; unit:ms; not used now
        setParam.u32PwmNumber = 0;		// PWM Num ID; Not used now
        setParam.u32PortIntDelay = 0;	// Port interrupt delay time

        s32Ret = CVI_ISP_SetCtrlParam(0, &setParam);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_ISP_SetCtrlParam failed with %#x\n", s32Ret);
            return s32Ret;
        }
    }

    return s32Ret;
}

#ifdef SUPPORT_ISP_PQTOOL
static CVI_VOID app_ipcam_Ispd_Load(CVI_VOID)
{
    // char *dlerr = NULL;
    // UNUSED(dlerr);
    if (!bISPDaemon) {
        isp_daemon2_init(ISPD_CONNECT_PORT);
        APP_PROF_LOG_PRINT(LEVEL_INFO, "Isp_daemon2_init %d success\n", ISPD_CONNECT_PORT);
        bISPDaemon = CVI_TRUE;
        //pISPDHandle = dlopen(ISPD_LIBNAME, RTLD_NOW);

        // dlerr = dlerror();
        // if (pISPDHandle) {
        //     APP_PROF_LOG_PRINT(LEVEL_INFO, "Load dynamic library %s success\n", ISPD_LIBNAME);

        //     void (*daemon_init)(unsigned int port);
        //     daemon_init = dlsym(pISPDHandle, "isp_daemon2_init");

        //     dlerr = dlerror();
        //     if (dlerr == NULL) {
        //         (*daemon_init)(ISPD_CONNECT_PORT);
        //         bISPDaemon = CVI_TRUE;
        //     } else {
        //         APP_PROF_LOG_PRINT(LEVEL_ERROR, "Run daemon initial failed with %s\n", dlerr);
        //         dlclose(pISPDHandle);
        //         pISPDHandle = NULL;
        //     }
        // } else {
        //     APP_PROF_LOG_PRINT(LEVEL_ERROR, "Load dynamic library %s failed with %s\n", ISPD_LIBNAME, dlerr);
        // }
    } else {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "%s already loaded\n", ISPD_LIBNAME);
    }
}

static CVI_VOID app_ipcam_Ispd_Unload(CVI_VOID)
{
    if (bISPDaemon) {
        // void (*daemon_uninit)(void);

        // daemon_uninit = dlsym(pISPDHandle, "isp_daemon_uninit");
        // if (dlerror() == NULL) {
        //     (*daemon_uninit)();
        // }

        // dlclose(pISPDHandle);
        // pISPDHandle = NULL;
        isp_daemon2_uninit();

        bISPDaemon = CVI_FALSE;
    } else {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "%s not load yet!\n", ISPD_LIBNAME);
    }
}

#ifndef ARCH_CV183X
static CVI_VOID app_ipcam_RawDump_Load(void)
{
    char *dlerr = NULL;

    if (!bRawDump) {
        pRawDumpHandle = dlopen(RAW_DUMP_LIBNAME, RTLD_NOW);

        dlerr = dlerror();

        if (pRawDumpHandle) {
            void (*cvi_raw_dump_init)(void);

            APP_PROF_LOG_PRINT(LEVEL_INFO, "Load dynamic library %s success\n", RAW_DUMP_LIBNAME);

            cvi_raw_dump_init = dlsym(pRawDumpHandle, "cvi_raw_dump_init");

            dlerr = dlerror();
            if (dlerr == NULL) {
                (*cvi_raw_dump_init)();
                bRawDump = CVI_TRUE;
            } else {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "Run raw dump initial fail, %s\n", dlerr);
                dlclose(pRawDumpHandle);
                pRawDumpHandle = NULL;
            }
        } else {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "dlopen: %s, dlerr: %s\n", RAW_DUMP_LIBNAME, dlerr);
        }
    } else {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "%s already loaded\n", RAW_DUMP_LIBNAME);
    }
}

static CVI_VOID app_ipcam_RawDump_Unload(CVI_VOID)
{
    if (bRawDump) {
        void (*daemon_uninit)(void);

        daemon_uninit = dlsym(pRawDumpHandle, "isp_daemon_uninit");
        if (dlerror() == NULL) {
            (*daemon_uninit)();
        }

        dlclose(pRawDumpHandle);
        pRawDumpHandle = NULL;
        bRawDump = CVI_FALSE;
    }
}
#endif
#endif

static int app_ipcam_Vi_framerate_Set(VI_PIPE ViPipe, CVI_S32 framerate)
{
    ISP_PUB_ATTR_S pubAttr = {0};

    CVI_ISP_GetPubAttr(ViPipe, &pubAttr);

    pubAttr.f32FrameRate = (CVI_FLOAT)framerate;

    APP_CHK_RET(CVI_ISP_SetPubAttr(ViPipe, &pubAttr), "set vi framerate");

    return CVI_SUCCESS;
}

static int app_ipcam_PQBin_Load(const CVI_CHAR *pBinPath)
{
    CVI_S32 ret = CVI_SUCCESS;
    FILE *fp = NULL;
    CVI_U8 *buf = NULL;
    CVI_U64 file_size;

    fp = fopen((const CVI_CHAR *)pBinPath, "rb");
    if (fp == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "Can't find bin(%s), use default parameters\n", pBinPath);
        return CVI_FAILURE;
    }

    fseek(fp, 0L, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);

    buf = (CVI_U8 *)malloc(file_size);
    if (buf == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "%s\n", "Allocae memory fail");
        fclose(fp);
        return CVI_FAILURE;
    }

    fread(buf, file_size, 1, fp);

    if (fp != NULL) {
        fclose(fp);
    }
    ret = CVI_BIN_ImportBinData(buf, (CVI_U32)file_size);
    if (ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_BIN_ImportBinData error! value:(0x%x)\n", ret);
        free(buf);
        return CVI_FAILURE;
    }

    free(buf);

    return CVI_SUCCESS;
}

#if 0
void app_ipcam_Framerate_Set(CVI_U8 viPipe, CVI_U8 fps)
{
    ISP_PUB_ATTR_S stPubAttr;

    memset(&stPubAttr, 0, sizeof(stPubAttr));

    CVI_ISP_GetPubAttr(viPipe, &stPubAttr);

    stPubAttr.f32FrameRate = fps;

    printf("set pipe: %d, fps: %d\n", viPipe, fps);

    CVI_ISP_SetPubAttr(viPipe, &stPubAttr);
}

CVI_U8 app_ipcam_Framerate_Get(CVI_U8 viPipe)
{
    ISP_PUB_ATTR_S stPubAttr;

    memset(&stPubAttr, 0, sizeof(stPubAttr));

    CVI_ISP_GetPubAttr(viPipe, &stPubAttr);

    return stPubAttr.f32FrameRate;
}
#endif

int app_ipcam_Vi_Isp_Init(void)
{
    CVI_S32 s32Ret;
    VI_PIPE              ViPipe;
    ISP_PUB_ATTR_S       stPubAttr;
    ISP_STATISTICS_CFG_S stsCfg;
    ISP_BIND_ATTR_S      stBindAttr;
    ALG_LIB_S            stAeLib;
    ALG_LIB_S            stAwbLib;

    APP_PARAM_VI_CTX_S *g_pstViCtx = app_ipcam_Vi_Param_Get();

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;

        stAeLib.s32Id = ViPipe;
        strcpy(stAeLib.acLibName, CVI_AE_LIB_NAME);//, sizeof(CVI_AE_LIB_NAME));
        s32Ret = CVI_AE_Register(ViPipe, &stAeLib);
        APP_IPCAM_CHECK_RET(s32Ret, "AE Algo register fail, ViPipe[%d]\n", ViPipe);

        stAwbLib.s32Id = ViPipe;
        strcpy(stAwbLib.acLibName, CVI_AWB_LIB_NAME);//, sizeof(CVI_AWB_LIB_NAME));
        s32Ret = CVI_AWB_Register(ViPipe, &stAwbLib);
        APP_IPCAM_CHECK_RET(s32Ret, "AWB Algo register fail, ViPipe[%d]\n", ViPipe);

        memset(&stBindAttr, 0, sizeof(ISP_BIND_ATTR_S));
        stBindAttr.sensorId = 0;
        snprintf(stBindAttr.stAeLib.acLibName, sizeof(CVI_AE_LIB_NAME), "%s", CVI_AE_LIB_NAME);
        stBindAttr.stAeLib.s32Id = ViPipe;
        snprintf(stBindAttr.stAwbLib.acLibName, sizeof(CVI_AWB_LIB_NAME), "%s", CVI_AWB_LIB_NAME);
        stBindAttr.stAwbLib.s32Id = ViPipe;
        s32Ret = CVI_ISP_SetBindAttr(ViPipe, &stBindAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "Bind Algo fail, ViPipe[%d]\n", ViPipe);

        s32Ret = CVI_ISP_MemInit(ViPipe);
        APP_IPCAM_CHECK_RET(s32Ret, "Init Ext memory fail, ViPipe[%d]\n", ViPipe);

        s32Ret = app_ipcam_Isp_PubAttr_Get(pstSnsCfg->enSnsType, &stPubAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "app_ipcam_Isp_PubAttr_Get(%d) failed!\n", ViPipe);

        stPubAttr.stWndRect.s32X = 0;
        stPubAttr.stWndRect.s32Y = 0;
        stPubAttr.stWndRect.u32Width  = pstChnCfg->u32Width;
        stPubAttr.stWndRect.u32Height = pstChnCfg->u32Height;
        stPubAttr.stSnsSize.u32Width  = pstChnCfg->u32Width;
        stPubAttr.stSnsSize.u32Height = pstChnCfg->u32Height;
        stPubAttr.f32FrameRate        = pstChnCfg->f32Fps;
        stPubAttr.enWDRMode           = pstSnsCfg->enWDRMode;
        s32Ret = CVI_ISP_SetPubAttr(ViPipe, &stPubAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "SetPubAttr fail, ViPipe[%d]\n", ViPipe);


        memset(&stsCfg, 0, sizeof(ISP_STATISTICS_CFG_S));
        s32Ret = CVI_ISP_GetStatisticsConfig(ViPipe, &stsCfg);
        APP_IPCAM_CHECK_RET(s32Ret, "ISP Get Statistic fail, ViPipe[%d]\n", ViPipe);
        stsCfg.stAECfg.stCrop[0].bEnable = 0;
        stsCfg.stAECfg.stCrop[0].u16X = 0;
        stsCfg.stAECfg.stCrop[0].u16Y = 0;
        stsCfg.stAECfg.stCrop[0].u16W = stPubAttr.stWndRect.u32Width;
        stsCfg.stAECfg.stCrop[0].u16H = stPubAttr.stWndRect.u32Height;
        memset(stsCfg.stAECfg.au8Weight, 1,
                AE_WEIGHT_ZONE_ROW * AE_WEIGHT_ZONE_COLUMN * sizeof(CVI_U8));

        // stsCfg.stAECfg.stCrop[1].bEnable = 0;
        // stsCfg.stAECfg.stCrop[1].u16X = 0;
        // stsCfg.stAECfg.stCrop[1].u16Y = 0;
        // stsCfg.stAECfg.stCrop[1].u16W = stPubAttr.stWndRect.u32Width;
        // stsCfg.stAECfg.stCrop[1].u16H = stPubAttr.stWndRect.u32Height;

        stsCfg.stWBCfg.u16ZoneRow = AWB_ZONE_ORIG_ROW;
        stsCfg.stWBCfg.u16ZoneCol = AWB_ZONE_ORIG_COLUMN;
        stsCfg.stWBCfg.stCrop.u16X = 0;
        stsCfg.stWBCfg.stCrop.u16Y = 0;
        stsCfg.stWBCfg.stCrop.u16W = stPubAttr.stWndRect.u32Width;
        stsCfg.stWBCfg.stCrop.u16H = stPubAttr.stWndRect.u32Height;
        stsCfg.stWBCfg.u16BlackLevel = 0;
        stsCfg.stWBCfg.u16WhiteLevel = 4095;
        stsCfg.stFocusCfg.stConfig.bEnable = 1;
        stsCfg.stFocusCfg.stConfig.u8HFltShift = 1;
        stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[0] = 1;
        stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[1] = 2;
        stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[2] = 3;
        stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[3] = 5;
        stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[4] = 10;
        stsCfg.stFocusCfg.stConfig.stRawCfg.PreGammaEn = 0;
        stsCfg.stFocusCfg.stConfig.stPreFltCfg.PreFltEn = 1;
        stsCfg.stFocusCfg.stConfig.u16Hwnd = 17;
        stsCfg.stFocusCfg.stConfig.u16Vwnd = 15;
        stsCfg.stFocusCfg.stConfig.stCrop.bEnable = 0;
        // AF offset and size has some limitation.
        stsCfg.stFocusCfg.stConfig.stCrop.u16X = AF_XOFFSET_MIN;
        stsCfg.stFocusCfg.stConfig.stCrop.u16Y = AF_YOFFSET_MIN;
        stsCfg.stFocusCfg.stConfig.stCrop.u16W = stPubAttr.stWndRect.u32Width - AF_XOFFSET_MIN * 2;
        stsCfg.stFocusCfg.stConfig.stCrop.u16H = stPubAttr.stWndRect.u32Height - AF_YOFFSET_MIN * 2;
        //Horizontal HP0
        stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[0] = 0;
        stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[1] = 0;
        stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[2] = 13;
        stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[3] = 24;
        stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[4] = 0;
        //Horizontal HP1
        stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[0] = 1;
        stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[1] = 2;
        stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[2] = 4;
        stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[3] = 8;
        stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[4] = 0;
        //Vertical HP
        stsCfg.stFocusCfg.stVParam_FIR.s8VFltHpCoeff[0] = 13;
        stsCfg.stFocusCfg.stVParam_FIR.s8VFltHpCoeff[1] = 24;
        stsCfg.stFocusCfg.stVParam_FIR.s8VFltHpCoeff[2] = 0;

        stsCfg.unKey.bit1FEAeGloStat = 1;
        stsCfg.unKey.bit1FEAeLocStat = 1;
        stsCfg.unKey.bit1AwbStat1 = 1;
        stsCfg.unKey.bit1AwbStat2 = 1;
        stsCfg.unKey.bit1FEAfStat = 1;

        //LDG
        stsCfg.stFocusCfg.stConfig.u8ThLow = 0;
        stsCfg.stFocusCfg.stConfig.u8ThHigh = 255;
        stsCfg.stFocusCfg.stConfig.u8GainLow = 30;
        stsCfg.stFocusCfg.stConfig.u8GainHigh = 20;
        stsCfg.stFocusCfg.stConfig.u8SlopLow = 8;
        stsCfg.stFocusCfg.stConfig.u8SlopHigh = 15;

        s32Ret = CVI_ISP_SetStatisticsConfig(ViPipe, &stsCfg);
        APP_IPCAM_CHECK_RET(s32Ret, "ISP Set Statistic fail, ViPipe[%d]\n", ViPipe);

        s32Ret = CVI_ISP_Init(ViPipe);
        APP_IPCAM_CHECK_RET(s32Ret, "ISP Init fail, ViPipe[%d]\n", ViPipe);

        s32Ret = app_ipcam_PQBin_Load(PQ_BIN_SDR);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_WARN, "load %s failed with %#x!\n", PQ_BIN_SDR, s32Ret);
        }
    }

    return CVI_SUCCESS;
}

int app_ipcam_Vi_Isp_DeInit(void)
{
    CVI_S32 s32Ret;
    VI_PIPE              ViPipe;
    ALG_LIB_S            ae_lib;
    ALG_LIB_S            awb_lib;

    ISP_SNS_OBJ_S *pfnSnsObj = CVI_NULL;
    APP_PARAM_VI_CTX_S *g_pstViCtx = app_ipcam_Vi_Param_Get();

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;

        pfnSnsObj = app_ipcam_SnsObj_Get(pstSnsCfg->enSnsType);
        if (pfnSnsObj == CVI_NULL) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"sensor obj(%d) is null\n", ViPipe);
            return CVI_FAILURE;
        }

        ae_lib.s32Id = ViPipe;
        awb_lib.s32Id = ViPipe;

        strcpy(ae_lib.acLibName, CVI_AE_LIB_NAME);//, sizeof(CVI_AE_LIB_NAME));
        strcpy(awb_lib.acLibName, CVI_AWB_LIB_NAME);//, sizeof(CVI_AWB_LIB_NAME));

        s32Ret = pfnSnsObj->pfnUnRegisterCallback(ViPipe, &ae_lib, &awb_lib);
        APP_IPCAM_CHECK_RET(s32Ret, "pfnUnRegisterCallback(%d) fail\n", ViPipe);

        s32Ret = CVI_AE_UnRegister(ViPipe, &ae_lib);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_AE_UnRegister(%d) fail\n", ViPipe);

        s32Ret = CVI_AWB_UnRegister(ViPipe, &awb_lib);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_AWB_UnRegister(%d) fail\n", ViPipe);

    }
    return CVI_SUCCESS;

}

static void callback_FPS(int fps)
{
    static CVI_FLOAT uMaxFPS[VI_MAX_DEV_NUM] = {0};
    CVI_U32 i;

    for (i = 0; i < VI_MAX_DEV_NUM && g_IspPid[i]; i++) {
        ISP_PUB_ATTR_S pubAttr = {0};

        CVI_ISP_GetPubAttr(i, &pubAttr);
        if (uMaxFPS[i] == 0) {
            uMaxFPS[i] = pubAttr.f32FrameRate;
        }
        if (fps == 0) {
            pubAttr.f32FrameRate = uMaxFPS[i];
        } else {
            pubAttr.f32FrameRate = (CVI_FLOAT) fps;
        }
        CVI_ISP_SetPubAttr(i, &pubAttr);
    }
}

static void *ISP_Thread(void *arg)
{
    CVI_S32 s32Ret;
    VI_PIPE ViPipe = *(VI_PIPE *)arg;
    char szThreadName[20];

    snprintf(szThreadName, sizeof(szThreadName), "ISP%d_RUN", ViPipe);
    prctl(PR_SET_NAME, szThreadName, 0, 0, 0);

    // if (ViPipe > 0) {
    //     APP_PROF_LOG_PRINT(LEVEL_ERROR,"ISP Dev %d return\n", ViPipe);
    //     return CVI_NULL;
    // }

    CVI_SYS_RegisterThermalCallback(callback_FPS);

    APP_PROF_LOG_PRINT(LEVEL_INFO, "ISP Dev %d running!\n", ViPipe);
    s32Ret = CVI_ISP_Run(ViPipe);
    if (s32Ret != 0) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR,"CVI_ISP_Run(%d) failed with %#x!\n", ViPipe, s32Ret);
    }

    return CVI_NULL;
}

int app_ipcam_Vi_Isp_Start(void)
{
    CVI_S32 s32Ret;
    struct sched_param param;
    pthread_attr_t attr;

    VI_PIPE ViPipe;
    APP_PARAM_VI_CTX_S *g_pstViCtx = app_ipcam_Vi_Param_Get();

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;

        param.sched_priority = 80;
        pthread_attr_init(&attr);
        pthread_attr_setschedpolicy(&attr, SCHED_RR);
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        s32Ret = pthread_create(&g_IspPid[ViPipe], &attr, ISP_Thread, (void *)&pstSnsCfg->s32SnsId);
        APP_IPCAM_CHECK_RET(s32Ret, "create isp running thread(%d) fail\n", ViPipe);
    }

    VI_DEV_ATTR_S pstDevAttr;
    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;
        CVI_VI_GetDevAttr(ViPipe, &pstDevAttr);
        s32Ret = CVI_BIN_SetBinName(pstDevAttr.stWDRAttr.enWDRMode, PQ_BIN_SDR);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_BIN_SetBinName %s failed with %#x!\n", PQ_BIN_SDR, s32Ret);
            return s32Ret;
        }

        s32Ret = app_ipcam_Vi_framerate_Set(ViPipe, pstSnsCfg->s32Framerate);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Vi_framerate_Set failed with %#x!\n", s32Ret);
            return s32Ret;
        }
    }

    s32Ret = app_ipcam_ISP_ProcInfo_Open(ISP_PROC_LOG_LEVEL_NONE);
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_ISP_ProcInfo_Open failed with %#x!\n", s32Ret);
        return s32Ret;
    }

#if 0
    bAfFilterEnable = g_pstViCtx->astIspCfg[0].bAfFliter;
    if (bAfFilterEnable) {
        s32Ret = app_ipcam_Isp_AfFilter_Start();
        if(s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Isp_AfFilter_Start failed with %#x\n", s32Ret);
            return s32Ret;
        }
    }
#endif

    #ifdef SUPPORT_ISP_PQTOOL
    app_ipcam_Ispd_Load();
    #ifndef ARCH_CV183X
    app_ipcam_RawDump_Load();
    #endif
    #endif

    return CVI_SUCCESS;
}


int app_ipcam_Vi_Isp_Stop(void)
{
    CVI_S32 s32Ret;
    VI_PIPE ViPipe;
    APP_PARAM_VI_CTX_S *g_pstViCtx = app_ipcam_Vi_Param_Get();

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;

        #ifdef SUPPORT_ISP_PQTOOL
        app_ipcam_Ispd_Unload();
        #ifndef ARCH_CV183X
        app_ipcam_RawDump_Unload();
        #endif
        #endif

        if (g_IspPid[ViPipe]) {
            s32Ret = CVI_ISP_Exit(ViPipe);
            if (s32Ret != CVI_SUCCESS) {
                 APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_ISP_Exit fail with %#x!\n", s32Ret);
                return CVI_FAILURE;
            }
            pthread_join(g_IspPid[ViPipe], NULL);
            g_IspPid[ViPipe] = 0;
            // SAMPLE_COMM_ISP_Sensor_UnRegiter_callback(ViPipe);
            // SAMPLE_COMM_ISP_Aelib_UnCallback(ViPipe);
            // SAMPLE_COMM_ISP_Awblib_UnCallback(ViPipe);
            // #if ENABLE_AF_LIB
            // SAMPLE_COMM_ISP_Aflib_UnCallback(ViPipe);
            // #endif
        }
    }

    return CVI_SUCCESS;
}






// sensors.c

static const VI_DEV_ATTR_S vi_dev_attr_base = {
    .enIntfMode = VI_MODE_MIPI,
    .enWorkMode = VI_WORK_MODE_1Multiplex,
    .enScanMode = VI_SCAN_PROGRESSIVE,
    .as32AdChnId = { -1, -1, -1, -1 },
    .enDataSeq = VI_DATA_SEQ_YUYV,
    .stSynCfg = {
        /*port_vsync    port_vsync_neg    port_hsync              port_hsync_neg*/
        VI_VSYNC_PULSE,
        VI_VSYNC_NEG_LOW,
        VI_HSYNC_VALID_SINGNAL,
        VI_HSYNC_NEG_HIGH,
        /*port_vsync_valid     port_vsync_valid_neg*/
        VI_VSYNC_VALID_SIGNAL,
        VI_VSYNC_VALID_NEG_HIGH,

        .stTimingBlank = {
            /*hsync_hfb  hsync_act  hsync_hhb*/
            0, 1920, 0,
            /*vsync0_vhb vsync0_act vsync0_hhb*/
            0, 1080, 0,
            /*vsync1_vhb vsync1_act vsync1_hhb*/
            0, 0, 0 },
    },
    .enInputDataType = VI_DATA_TYPE_RGB,
    .stSize = { 1920, 1080 },
    .stWDRAttr = { WDR_MODE_NONE, 1080 },
    .enBayerFormat = BAYER_FORMAT_BG,
};

static const VI_PIPE_ATTR_S vi_pipe_attr_base = {
    .enPipeBypassMode = VI_PIPE_BYPASS_NONE,
    .bYuvSkip = CVI_FALSE,
    .bIspBypass = CVI_FALSE,
    .u32MaxW = 1920,
    .u32MaxH = 1080,
    .enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP,
    .enCompressMode = COMPRESS_MODE_NONE,
    .enBitWidth = DATA_BITWIDTH_12,
    .bNrEn = CVI_TRUE,
    .bSharpenEn = CVI_FALSE,
    .stFrameRate = { -1, -1 },
    .bDiscardProPic = CVI_FALSE,
    .bYuvBypassPath = CVI_FALSE,
};

static const VI_CHN_ATTR_S vi_chn_attr_base = {
    .stSize = { 1920, 1080 },
    .enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_420,
    .enDynamicRange = DYNAMIC_RANGE_SDR8,
    .enVideoFormat = VIDEO_FORMAT_LINEAR,
    .enCompressMode = COMPRESS_MODE_NONE,
    .bMirror = CVI_FALSE,
    .bFlip = CVI_FALSE,
    .u32Depth = 0,
    .stFrameRate = { -1, -1 },
};

static const ISP_PUB_ATTR_S isp_pub_attr_base = {
    .stWndRect = { 0, 0, 1920, 1080 },
    .stSnsSize = { 1920, 1080 },
    .f32FrameRate = 25.0f,
    .enBayer = BAYER_BGGR,
    .enWDRMode = WDR_MODE_NONE,
    .u8SnsMode = 0,
};

ISP_SNS_OBJ_S* app_ipcam_SnsObj_Get(SENSOR_TYPE_E enSnsType)
{
    switch (enSnsType) {
#ifdef SNS0_GCORE_GC1054
    case SENSOR_GCORE_GC1054:
        return &stSnsGc1054_Obj;
#endif
#ifdef SNS0_GCORE_GC2053
    case SENSOR_GCORE_GC2053:
        return &stSnsGc2053_Obj;
#endif
#ifdef SNS0_GCORE_GC2053_1L
    case SENSOR_GCORE_GC2053_1L:
        return &stSnsGc2053_1l_Obj;
#endif
#ifdef SNS1_GCORE_GC2053_SLAVE
    case SENSOR_GCORE_GC2053_SLAVE:
        return &stSnsGc2053_Slave_Obj;
#endif
#ifdef SNS0_GCORE_GC2093
    case SENSOR_GCORE_GC2093:
        return &stSnsGc2093_Obj;
#endif
#ifdef SNS1_GCORE_GC2093_SLAVE
    case SENSOR_GCORE_GC2093_SLAVE:
        return &stSnsGc2093_Slave_Obj;
#endif
#ifdef SNS0_GCORE_GC4023
    case SENSOR_GCORE_GC4023:
        return &stSnsGc4023_Obj;
#endif
#ifdef SNS0_GCORE_GC4653
    case SENSOR_GCORE_GC4653:
        return &stSnsGc4653_Obj;
#endif
#ifdef SNS1_GCORE_GC4653_SLAVE
    case SENSOR_GCORE_GC4653_SLAVE:
        return &stSnsGc4653_Slave_Obj;
#endif
#ifdef SNS0_NEXTCHIP_N5
    case SENSOR_NEXTCHIP_N5:
        return &stSnsN5_Obj;
#endif
#ifdef SNS0_NEXTCHIP_N6
    case SENSOR_NEXTCHIP_N6:
        return &stSnsN6_Obj;
#endif
#ifdef SNS0_OV_OV5647
    case SENSOR_OV_OV5647:
        return &stSnsOv5647_Obj;
#endif
#ifdef SNS0_OV_OS08A20
    case SENSOR_OV_OS08A20:
        return &stSnsOs08a20_Obj;
#endif
#ifdef SNS1_OV_OS08A20_SLAVE
    case SENSOR_OV_OS08A20_SLAVE:
        return &stSnsOs08a20_Slave_Obj;
#endif
#ifdef PICO_384
    case SENSOR_PICO_384:
        return &stSnsPICO384_Obj;
#endif
#ifdef SNS0_PICO_640
    case SENSOR_PICO_640:
        return &stSnsPICO640_Obj;
#endif
#ifdef SNS1_PIXELPLUS_PR2020
    case SENSOR_PIXELPLUS_PR2020:
        return &stSnsPR2020_Obj;
#endif
#ifdef SNS0_PIXELPLUS_PR2100
    case SENSOR_PIXELPLUS_PR2100:
        return &stSnsPR2100_Obj;
#endif
#ifdef SNS0_SMS_SC1346_1L
    case SENSOR_SMS_SC1346_1L:
    case SENSOR_SMS_SC1346_1L_60:
        return &stSnsSC1346_1L_Obj;
#endif
#ifdef SNS0_SMS_SC200AI
    case SENSOR_SMS_SC200AI:
        return &stSnsSC200AI_Obj;
#endif
#ifdef SNS0_SMS_SC2331_1L
    case SENSOR_SMS_SC2331_1L:
        return &stSnsSC2331_1L_Obj;
#endif
#ifdef SNS0_SMS_SC2335
    case SENSOR_SMS_SC2335:
        return &stSnsSC2335_Obj;
#endif
#ifdef SNS0_SMS_SC2336
    case SENSOR_SMS_SC2336:
        return &stSnsSC2336_Obj;
#endif
#ifdef SNS0_SMS_SC2336P
    case SENSOR_SMS_SC2336P:
        return &stSnsSC2336P_Obj;
#endif
#ifdef SNS0_SMS_SC3335
    case SENSOR_SMS_SC3335:
        return &stSnsSC3335_Obj;
#endif
#ifdef SNS1_SMS_SC3335_SLAVE
    case SENSOR_SMS_SC3335_SLAVE:
        return &stSnsSC3335_Slave_Obj;
#endif
#ifdef SNS0_SMS_SC3336
    case SENSOR_SMS_SC3336:
        return &stSnsSC3336_Obj;
#endif
#ifdef SNS0_SMS_SC401AI
    case SENSOR_SMS_SC401AI:
        return &stSnsSC401AI_Obj;
#endif
#ifdef SNS0_SMS_SC4210
    case SENSOR_SMS_SC4210:
        return &stSnsSC4210_Obj;
#endif
#ifdef SNS0_SMS_SC8238
    case SENSOR_SMS_SC8238:
        return &stSnsSC8238_Obj;
#endif
#ifdef SNS0_SMS_SC530AI_2L
    case SENSOR_SMS_SC530AI_2L:
        return &stSnsSC530AI_2L_Obj;
#endif
#ifdef SNS0_SMS_SC531AI_2L
    case SENSOR_SMS_SC531AI_2L:
        return &stSnsSC531AI_2L_Obj;
#endif
#ifdef SNS0_SMS_SC5336_2L
    case SENSOR_SMS_SC5336_2L:
        return &stSnsSC5336_2L_Obj;
#endif
#ifdef SNS0_SMS_SC4336P
    case SENSOR_SMS_SC4336P:
        return &stSnsSC4336P_Obj;
#endif
#ifdef SNS0_SOI_F23
    case SENSOR_SOI_F23:
        return &stSnsF23_Obj;
#endif
#ifdef SNS0_SOI_F35
    case SENSOR_SOI_F35:
        return &stSnsF35_Obj;
#endif
#ifdef SNS1_SOI_F35_SLAVE
    case SENSOR_SOI_F35_SLAVE:
        return &stSnsF35_Slave_Obj;
#endif
#ifdef SNS0_SOI_H65
    case SENSOR_SOI_H65:
        return &stSnsH65_Obj;
#endif
#ifdef SNS0_SOI_K06
    case SENSOR_SOI_K06:
        return &stSnsK06_Obj;
#endif
#ifdef SNS0_SOI_Q03P
    case SENSOR_SOI_Q03P:
        return &stSnsQ03P_Obj;
#endif
#ifdef SNS0_SONY_IMX290_2L
    case SENSOR_SONY_IMX290_2L:
        return &stSnsImx290_2l_Obj;
#endif
#ifdef SNS0_SONY_IMX307
    case SENSOR_SONY_IMX307:
        return &stSnsImx307_Obj;
#endif
#ifdef SNS0_SONY_IMX307_2L
    case SENSOR_SONY_IMX307_2L:
        return &stSnsImx307_2l_Obj;
#endif
#ifdef SNS1_SONY_IMX307_SLAVE
    case SENSOR_SONY_IMX307_SLAVE:
        return &stSnsImx307_Slave_Obj;
#endif
#ifdef SNS0_SONY_IMX307_SUBLVDS
    case SENSOR_SONY_IMX307_SUBLVDS:
        return &stSnsImx307_Sublvds_Obj;
#endif
#ifdef SNS0_SONY_IMX327
    case SENSOR_SONY_IMX327:
        return &stSnsImx327_Obj;
#endif
#ifdef SNS0_SONY_IMX327_2L
    case SENSOR_SONY_IMX327_2L:
        return &stSnsImx327_2l_Obj;
#endif
#ifdef SNS1_SONY_IMX327_SLAVE
    case SENSOR_SONY_IMX327_SLAVE:
        return &stSnsImx327_Slave_Obj;
#endif
#ifdef SNS0_SONY_IMX327_SUBLVDS
    case SENSOR_SONY_IMX327_SUBLVDS:
        return &stSnsImx327_Sublvds_Obj;
#endif
#ifdef SNS0_SONY_IMX334
    case SENSOR_SONY_IMX334:
        return &stSnsImx334_Obj;
#endif
#ifdef SNS0_SONY_IMX335
    case SENSOR_SONY_IMX335:
        return &stSnsImx335_Obj;
#endif
#ifdef SNS0_SONY_IMX347
    case SENSOR_SONY_IMX347:
        return &stSnsImx347_Obj;
#endif
#ifdef SNS0_SONY_IMX385
    case SENSOR_SONY_IMX385:
        return &stSnsImx385_Obj;
#endif
#ifdef SNS0_VIVO_MCS369
    case SENSOR_VIVO_MCS369:
        return &stSnsMCS369_Obj;
#endif
#ifdef SNS0_VIVO_MCS369Q
    case SENSOR_VIVO_MCS369Q:
        return &stSnsMCS369Q_Obj;
#endif
#ifdef SNS0_VIVO_MM308M2
    case SENSOR_VIVO_MM308M2:
        return &stSnsMM308M2_Obj;
#endif
#ifdef SENSOR_ID_MIS2008
    case SENSOR_IMGDS_MIS2008:
        return &stSnsMIS2008_Obj;
#endif
#ifdef SENSOR_ID_MIS2008_1L
    case SENSOR_IMGDS_MIS2008_1L:
        return &stSnsMIS2008_1L_Obj;
#endif
    default:
        return CVI_NULL;
    }
}

CVI_S32 app_ipcam_Vi_DevAttr_Get(SENSOR_TYPE_E enSnsType, VI_DEV_ATTR_S* pstViDevAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memcpy(pstViDevAttr, &vi_dev_attr_base, sizeof(VI_DEV_ATTR_S));

    switch (enSnsType) {
    case SENSOR_SMS_SC530AI_2L:
        pstViDevAttr->stSynCfg.stTimingBlank.u32HsyncAct = 2880;
        pstViDevAttr->stSynCfg.stTimingBlank.u32VsyncVact = 1620;
        pstViDevAttr->stSize.u32Width = 2880;
        pstViDevAttr->stSize.u32Height = 1620;
        pstViDevAttr->stWDRAttr.u32CacheLine = 1620;
        break;
    }

    switch (enSnsType) {
    case SENSOR_GCORE_GC1054:
    case SENSOR_GCORE_GC2053:
    case SENSOR_GCORE_GC2053_1L:
    case SENSOR_GCORE_GC2053_SLAVE:
    case SENSOR_GCORE_GC2093:
    case SENSOR_GCORE_GC4023:
    case SENSOR_GCORE_GC2093_SLAVE:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case SENSOR_GCORE_GC4653:
    case SENSOR_GCORE_GC4653_SLAVE:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_GR;
        break;
    case SENSOR_NEXTCHIP_N5:
        pstViDevAttr->enIntfMode = VI_MODE_BT656;
        pstViDevAttr->enDataSeq = VI_DATA_SEQ_UYVY;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        break;
    case SENSOR_NEXTCHIP_N6:
        pstViDevAttr->enDataSeq = VI_DATA_SEQ_UYVY;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        break;
    case SENSOR_OV_OS08A20:
    case SENSOR_OV_OS08A20_SLAVE:
    case SENSOR_OV_OV5647:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        break;
    case SENSOR_PICO_384:
    case SENSOR_PICO_640:
        break;
    case SENSOR_PIXELPLUS_PR2020:
        pstViDevAttr->enIntfMode = VI_MODE_BT656;
        pstViDevAttr->enDataSeq = VI_DATA_SEQ_UYVY;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        break;
    case SENSOR_PIXELPLUS_PR2100:
        pstViDevAttr->enIntfMode = VI_MODE_MIPI_YUV422;
        pstViDevAttr->enDataSeq = VI_DATA_SEQ_UYVY;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        break;
    case SENSOR_SMS_SC1346_1L:
    case SENSOR_SMS_SC1346_1L_60:
    case SENSOR_SMS_SC200AI:
    case SENSOR_SMS_SC2331_1L:
    case SENSOR_SMS_SC2335:
    case SENSOR_SMS_SC2336:
    case SENSOR_SMS_SC2336P:
    case SENSOR_SMS_SC3335:
    case SENSOR_SMS_SC3335_SLAVE:
    case SENSOR_SMS_SC3336:
    case SENSOR_SMS_SC401AI:
    case SENSOR_SMS_SC4210:
    case SENSOR_SMS_SC8238:
    case SENSOR_SMS_SC530AI_2L:
    case SENSOR_SMS_SC531AI_2L:
    case SENSOR_SMS_SC5336_2L:
    case SENSOR_SMS_SC4336P:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        break;
    case SENSOR_SOI_F23:
    case SENSOR_SOI_F35:
    case SENSOR_SOI_F35_SLAVE:
    case SENSOR_SOI_H65:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        break;
    case SENSOR_SOI_K06:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_GB;
        break;
    case SENSOR_SOI_Q03P:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        break;
    case SENSOR_SONY_IMX290_2L:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        break;
    case SENSOR_SONY_IMX307:
    case SENSOR_SONY_IMX307_2L:
    case SENSOR_SONY_IMX307_SLAVE:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case SENSOR_SONY_IMX307_SUBLVDS:
        pstViDevAttr->enIntfMode = VI_MODE_LVDS;
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case SENSOR_SONY_IMX327:
    case SENSOR_SONY_IMX327_2L:
    case SENSOR_SONY_IMX327_SLAVE:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case SENSOR_SONY_IMX327_SUBLVDS:
        pstViDevAttr->enIntfMode = VI_MODE_LVDS;
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case SENSOR_SONY_IMX334:
    case SENSOR_SONY_IMX335:
    case SENSOR_SONY_IMX347:
    case SENSOR_SONY_IMX385:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case SENSOR_VIVO_MCS369:
    case SENSOR_VIVO_MCS369Q:
    case SENSOR_VIVO_MM308M2:
        pstViDevAttr->enIntfMode = VI_MODE_BT1120_STANDARD;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        break;
    case SENSOR_IMGDS_MIS2008:
    case SENSOR_IMGDS_MIS2008_1L:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    default:
        s32Ret = CVI_FAILURE;
        break;
    }
    return s32Ret;
}

CVI_S32 app_ipcam_Vi_PipeAttr_Get(SENSOR_TYPE_E enSnsType, VI_PIPE_ATTR_S* pstViPipeAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memcpy(pstViPipeAttr, &vi_pipe_attr_base, sizeof(VI_PIPE_ATTR_S));

    switch (enSnsType) {
    case SENSOR_SMS_SC530AI_2L:
        pstViPipeAttr->u32MaxW = 2880;
        pstViPipeAttr->u32MaxH = 1620;
        break;
    }

    switch (enSnsType) {
    case SENSOR_GCORE_GC1054:
    case SENSOR_GCORE_GC2053:
    case SENSOR_GCORE_GC2053_1L:
    case SENSOR_OV_OV5647:
    case SENSOR_GCORE_GC2053_SLAVE:
    case SENSOR_GCORE_GC2093:
    case SENSOR_GCORE_GC2093_SLAVE:
    case SENSOR_GCORE_GC4023:
    case SENSOR_GCORE_GC4653:
    case SENSOR_GCORE_GC4653_SLAVE:
        break;
    case SENSOR_NEXTCHIP_N5:
    case SENSOR_NEXTCHIP_N6:
        pstViPipeAttr->bYuvBypassPath = CVI_TRUE;
        break;
    case SENSOR_OV_OS08A20:
    case SENSOR_OV_OS08A20_SLAVE:
        break;
    case SENSOR_PICO_384:
    case SENSOR_PICO_640:
    case SENSOR_PIXELPLUS_PR2020:
    case SENSOR_PIXELPLUS_PR2100:
        pstViPipeAttr->bYuvBypassPath = CVI_TRUE;
        break;
    case SENSOR_SMS_SC1346_1L:
    case SENSOR_SMS_SC1346_1L_60:
    case SENSOR_SMS_SC200AI:
    case SENSOR_SMS_SC2331_1L:
    case SENSOR_SMS_SC2335:
    case SENSOR_SMS_SC2336:
    case SENSOR_SMS_SC2336P:
    case SENSOR_SMS_SC3335:
    case SENSOR_SMS_SC3335_SLAVE:
    case SENSOR_SMS_SC3336:
    case SENSOR_SMS_SC401AI:
    case SENSOR_SMS_SC4210:
    case SENSOR_SMS_SC8238:
    case SENSOR_SMS_SC530AI_2L:
    case SENSOR_SMS_SC531AI_2L:
    case SENSOR_SMS_SC5336_2L:
    case SENSOR_SMS_SC4336P:
    case SENSOR_SOI_F23:
    case SENSOR_SOI_F35:
    case SENSOR_SOI_F35_SLAVE:
    case SENSOR_SOI_H65:
    case SENSOR_SOI_K06:
    case SENSOR_SOI_Q03P:
    case SENSOR_SONY_IMX290_2L:
    case SENSOR_SONY_IMX307:
    case SENSOR_SONY_IMX307_2L:
    case SENSOR_SONY_IMX307_SLAVE:
    case SENSOR_SONY_IMX307_SUBLVDS:
    case SENSOR_SONY_IMX327:
    case SENSOR_SONY_IMX327_2L:
    case SENSOR_SONY_IMX327_SLAVE:
    case SENSOR_SONY_IMX327_SUBLVDS:
    case SENSOR_SONY_IMX334:
    case SENSOR_SONY_IMX335:
    case SENSOR_SONY_IMX347:
    case SENSOR_SONY_IMX385:
    case SENSOR_IMGDS_MIS2008:
    case SENSOR_IMGDS_MIS2008_1L:
        break;
    case SENSOR_VIVO_MCS369:
    case SENSOR_VIVO_MCS369Q:
    case SENSOR_VIVO_MM308M2:
        pstViPipeAttr->bYuvBypassPath = CVI_TRUE;
        break;
    default:
        s32Ret = CVI_FAILURE;
        break;
    }
    return s32Ret;
}

CVI_S32 app_ipcam_Vi_ChnAttr_Get(SENSOR_TYPE_E enSnsType, VI_CHN_ATTR_S* pstViChnAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memcpy(pstViChnAttr, &vi_chn_attr_base, sizeof(VI_CHN_ATTR_S));

    switch (enSnsType) {
    case SENSOR_SMS_SC530AI_2L:
        pstViChnAttr->stSize.u32Width = 2880;
        pstViChnAttr->stSize.u32Height = 1620;
        break;
    }

    switch (enSnsType) {
    case SENSOR_GCORE_GC1054:
    case SENSOR_OV_OV5647:
    case SENSOR_GCORE_GC2053:
    case SENSOR_GCORE_GC2053_1L:
    case SENSOR_GCORE_GC2053_SLAVE:
    case SENSOR_GCORE_GC2093:
    case SENSOR_GCORE_GC2093_SLAVE:
    case SENSOR_GCORE_GC4023:
    case SENSOR_GCORE_GC4653:
    case SENSOR_GCORE_GC4653_SLAVE:
        break;
    case SENSOR_NEXTCHIP_N5:
    case SENSOR_NEXTCHIP_N6:
        pstViChnAttr->enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_422;
        break;
    case SENSOR_OV_OS08A20:
    case SENSOR_OV_OS08A20_SLAVE:
        break;
    case SENSOR_PICO_384:
    case SENSOR_PICO_640:
    case SENSOR_PIXELPLUS_PR2020:
    case SENSOR_PIXELPLUS_PR2100:
        pstViChnAttr->enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_422;
        break;
    case SENSOR_SMS_SC1346_1L:
    case SENSOR_SMS_SC1346_1L_60:
    case SENSOR_SMS_SC200AI:
    case SENSOR_SMS_SC2331_1L:
    case SENSOR_SMS_SC2335:
    case SENSOR_SMS_SC2336:
    case SENSOR_SMS_SC2336P:
    case SENSOR_SMS_SC3335:
    case SENSOR_SMS_SC3335_SLAVE:
    case SENSOR_SMS_SC3336:
    case SENSOR_SMS_SC401AI:
    case SENSOR_SMS_SC4210:
    case SENSOR_SMS_SC8238:
    case SENSOR_SMS_SC530AI_2L:
    case SENSOR_SMS_SC531AI_2L:
    case SENSOR_SMS_SC5336_2L:
    case SENSOR_SMS_SC4336P:
    case SENSOR_SOI_F23:
    case SENSOR_SOI_F35:
    case SENSOR_SOI_F35_SLAVE:
    case SENSOR_SOI_H65:
    case SENSOR_SOI_K06:
    case SENSOR_SOI_Q03P:
    case SENSOR_SONY_IMX290_2L:
    case SENSOR_SONY_IMX307:
    case SENSOR_SONY_IMX307_2L:
    case SENSOR_SONY_IMX307_SLAVE:
    case SENSOR_SONY_IMX307_SUBLVDS:
    case SENSOR_SONY_IMX327:
    case SENSOR_SONY_IMX327_2L:
    case SENSOR_SONY_IMX327_SLAVE:
    case SENSOR_SONY_IMX327_SUBLVDS:
    case SENSOR_SONY_IMX334:
    case SENSOR_SONY_IMX335:
    case SENSOR_SONY_IMX347:
    case SENSOR_SONY_IMX385:
    case SENSOR_IMGDS_MIS2008:
    case SENSOR_IMGDS_MIS2008_1L:
        break;
    case SENSOR_VIVO_MCS369:
    case SENSOR_VIVO_MCS369Q:
    case SENSOR_VIVO_MM308M2:
        pstViChnAttr->enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_422;
        break;
    default:
        s32Ret = CVI_FAILURE;
        break;
    }
    return s32Ret;
}

CVI_S32 app_ipcam_Isp_InitAttr_Get(SENSOR_TYPE_E enSnsType, WDR_MODE_E enWDRMode, ISP_INIT_ATTR_S* pstIspInitAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memset(pstIspInitAttr, 0, sizeof(ISP_INIT_ATTR_S));

    switch (enSnsType) {
    case SENSOR_GCORE_GC1054:
    case SENSOR_GCORE_GC2053:
    case SENSOR_GCORE_GC2053_1L:
    case SENSOR_OV_OV5647:
    case SENSOR_GCORE_GC2053_SLAVE:
    case SENSOR_GCORE_GC2093:
    case SENSOR_GCORE_GC2093_SLAVE:
    case SENSOR_GCORE_GC4023:
    case SENSOR_GCORE_GC4653:
    case SENSOR_GCORE_GC4653_SLAVE:
    case SENSOR_NEXTCHIP_N5:
    case SENSOR_NEXTCHIP_N6:
        break;
    case SENSOR_OV_OS08A20:
    case SENSOR_OV_OS08A20_SLAVE:
        if (enWDRMode == WDR_MODE_2To1_LINE) {
            pstIspInitAttr->enL2SMode = SNS_L2S_MODE_FIX;
        }
        break;
    case SENSOR_PICO_384:
    case SENSOR_PICO_640:
    case SENSOR_PIXELPLUS_PR2020:
    case SENSOR_PIXELPLUS_PR2100:
    case SENSOR_SMS_SC1346_1L:
    case SENSOR_SMS_SC1346_1L_60:
    case SENSOR_SMS_SC200AI:
    case SENSOR_SMS_SC2331_1L:
    case SENSOR_SMS_SC2335:
    case SENSOR_SMS_SC2336:
    case SENSOR_SMS_SC2336P:
    case SENSOR_SMS_SC3335:
    case SENSOR_SMS_SC3335_SLAVE:
    case SENSOR_SMS_SC3336:
    case SENSOR_SMS_SC401AI:
    case SENSOR_SMS_SC4210:
    case SENSOR_SMS_SC8238:
    case SENSOR_SMS_SC530AI_2L:
    case SENSOR_SMS_SC531AI_2L:
    case SENSOR_SMS_SC5336_2L:
    case SENSOR_SMS_SC4336P:
    case SENSOR_SOI_F23:
    case SENSOR_IMGDS_MIS2008:
    case SENSOR_IMGDS_MIS2008_1L:
        break;
    case SENSOR_SOI_F35:
    case SENSOR_SOI_F35_SLAVE:
        if (enWDRMode == WDR_MODE_2To1_LINE) {
            pstIspInitAttr->enL2SMode = SNS_L2S_MODE_FIX;
        }
        break;
    case SENSOR_SOI_H65:
    case SENSOR_SOI_K06:
    case SENSOR_SOI_Q03P:
    case SENSOR_SONY_IMX290_2L:
    case SENSOR_SONY_IMX307:
    case SENSOR_SONY_IMX307_2L:
    case SENSOR_SONY_IMX307_SLAVE:
    case SENSOR_SONY_IMX307_SUBLVDS:
    case SENSOR_SONY_IMX327:
    case SENSOR_SONY_IMX327_2L:
    case SENSOR_SONY_IMX327_SLAVE:
    case SENSOR_SONY_IMX327_SUBLVDS:
    case SENSOR_SONY_IMX334:
    case SENSOR_SONY_IMX335:
    case SENSOR_SONY_IMX347:
    case SENSOR_SONY_IMX385:
    case SENSOR_VIVO_MCS369:
    case SENSOR_VIVO_MCS369Q:
    case SENSOR_VIVO_MM308M2:
        break;
    default:
        s32Ret = CVI_FAILURE;
        break;
    }
    return s32Ret;
}

CVI_S32 app_ipcam_Isp_PubAttr_Get(SENSOR_TYPE_E enSnsType, ISP_PUB_ATTR_S* pstIspPubAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memcpy(pstIspPubAttr, &isp_pub_attr_base, sizeof(ISP_PUB_ATTR_S));
    switch (enSnsType) {
    case SENSOR_SMS_SC530AI_2L:
        pstIspPubAttr->stWndRect.u32Width = 2880;
        pstIspPubAttr->stWndRect.u32Height = 1620;
        pstIspPubAttr->stSnsSize.u32Width = 2880;
        pstIspPubAttr->stSnsSize.u32Height = 1620;
        break;
    }

    // FPS
    switch (enSnsType) {
    case SENSOR_SMS_SC1346_1L_60:
        pstIspPubAttr->f32FrameRate = 60;
        break;
    default:
        pstIspPubAttr->f32FrameRate = 25;
        break;
    }
    switch (enSnsType) {
    case SENSOR_GCORE_GC1054:
    case SENSOR_GCORE_GC2053:
    case SENSOR_GCORE_GC2053_1L:
    case SENSOR_GCORE_GC2053_SLAVE:
    case SENSOR_GCORE_GC2093:
    case SENSOR_GCORE_GC4023:
    case SENSOR_GCORE_GC2093_SLAVE:
        pstIspPubAttr->enBayer = BAYER_RGGB;
        break;
    case SENSOR_GCORE_GC4653:
    case SENSOR_GCORE_GC4653_SLAVE:
        pstIspPubAttr->enBayer = BAYER_GRBG;
        break;
    case SENSOR_NEXTCHIP_N5:
    case SENSOR_NEXTCHIP_N6:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_OV_OS08A20:
    case SENSOR_OV_OS08A20_SLAVE:
    case SENSOR_OV_OV5647:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_PICO_384:
    case SENSOR_PICO_640:
    case SENSOR_PIXELPLUS_PR2020:
    case SENSOR_PIXELPLUS_PR2100:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_SMS_SC1346_1L:
    case SENSOR_SMS_SC1346_1L_60:
    case SENSOR_SMS_SC200AI:
    case SENSOR_SMS_SC2331_1L:
    case SENSOR_SMS_SC2335:
    case SENSOR_SMS_SC2336:
    case SENSOR_SMS_SC2336P:
    case SENSOR_SMS_SC3335:
    case SENSOR_SMS_SC3335_SLAVE:
    case SENSOR_SMS_SC3336:
    case SENSOR_SMS_SC401AI:
    case SENSOR_SMS_SC4210:
    case SENSOR_SMS_SC8238:
    case SENSOR_SMS_SC530AI_2L:
    case SENSOR_SMS_SC531AI_2L:
    case SENSOR_SMS_SC5336_2L:
    case SENSOR_SMS_SC4336P:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_SOI_F23:
    case SENSOR_SOI_F35:
    case SENSOR_SOI_F35_SLAVE:
    case SENSOR_SOI_H65:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_SOI_K06:
        pstIspPubAttr->enBayer = BAYER_GBRG;
        break;
    case SENSOR_SOI_Q03P:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_SONY_IMX290_2L:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_SONY_IMX307:
    case SENSOR_SONY_IMX307_2L:
    case SENSOR_SONY_IMX307_SLAVE:
    case SENSOR_SONY_IMX307_SUBLVDS:
    case SENSOR_SONY_IMX327:
    case SENSOR_SONY_IMX327_2L:
    case SENSOR_SONY_IMX327_SLAVE:
    case SENSOR_SONY_IMX327_SUBLVDS:
    case SENSOR_SONY_IMX334:
    case SENSOR_SONY_IMX335:
    case SENSOR_SONY_IMX347:
    case SENSOR_SONY_IMX385:
        pstIspPubAttr->enBayer = BAYER_RGGB;
        break;
    case SENSOR_VIVO_MCS369:
    case SENSOR_VIVO_MCS369Q:
    case SENSOR_VIVO_MM308M2:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    case SENSOR_IMGDS_MIS2008:
    case SENSOR_IMGDS_MIS2008_1L:
        pstIspPubAttr->enBayer = BAYER_RGGB;
        break;
    default:
        s32Ret = CVI_FAILURE;
        break;
    }
    return s32Ret;
}






// vi.c


/**************************************************************************
 *                              M A C R O S                               *
 **************************************************************************/

/**************************************************************************
 *                           C O N S T A N T S                            *
 **************************************************************************/

/**************************************************************************
 *                          D A T A    T Y P E S                          *
 **************************************************************************/

/**************************************************************************
 *                         G L O B A L    D A T A                         *
 **************************************************************************/

static APP_PARAM_VI_CTX_S g_stViCtx, *g_pstViCtx = &g_stViCtx;
// static APP_PARAM_VI_PM_DATA_S ViPmData[VI_MAX_DEV_NUM] = { 0 };

/**************************************************************************
 *                 E X T E R N A L    R E F E R E N C E S                 *
 **************************************************************************/

/**************************************************************************
 *               F U N C T I O N    D E C L A R A T I O N S               *
 **************************************************************************/

APP_PARAM_VI_CTX_S *app_ipcam_Vi_Param_Get(void)
{
    return g_pstViCtx;
}

static int app_ipcam_Vi_Sensor_Start(void)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    CVI_S32 s32SnsId;
    VI_PIPE ViPipe;

    ISP_SNS_OBJ_S *pfnSnsObj = CVI_NULL;
    RX_INIT_ATTR_S rx_init_attr;
    ISP_INIT_ATTR_S isp_init_attr;
    ISP_SNS_COMMBUS_U sns_bus_info;
    ALG_LIB_S ae_lib;
    ALG_LIB_S awb_lib;
    ISP_SENSOR_EXP_FUNC_S isp_sensor_exp_func;
    ISP_PUB_ATTR_S stPubAttr;
    ISP_CMOS_SENSOR_IMAGE_MODE_S isp_cmos_sensor_image_mode;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        s32SnsId = pstSnsCfg->s32SnsId;
        ViPipe   = pstChnCfg->s32ChnId;

        if (s32SnsId >= VI_MAX_DEV_NUM) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "invalid sensor id: %d\n", s32SnsId);
            return CVI_FAILURE;
        }

        APP_PROF_LOG_PRINT(LEVEL_INFO, "enSnsType enum: %d\n", pstSnsCfg->enSnsType);
        pfnSnsObj = app_ipcam_SnsObj_Get(pstSnsCfg->enSnsType);
        if (pfnSnsObj == CVI_NULL) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"sensor obj(%d) is null\n", ViPipe);
            return CVI_FAILURE;
        }

        memset(&rx_init_attr, 0, sizeof(RX_INIT_ATTR_S));
        rx_init_attr.MipiDev       = pstSnsCfg->MipiDev;
        if (pstSnsCfg->bMclkEn) {
            rx_init_attr.stMclkAttr.bMclkEn = CVI_TRUE;
            rx_init_attr.stMclkAttr.u8Mclk  = pstSnsCfg->u8Mclk;
        }

        for (CVI_U32 i = 0; i < (CVI_U32)sizeof(rx_init_attr.as16LaneId)/sizeof(CVI_S16); i++) {
            rx_init_attr.as16LaneId[i] = pstSnsCfg->as16LaneId[i];
        }
        for (CVI_U32 i = 0; i < (CVI_U32)sizeof(rx_init_attr.as8PNSwap)/sizeof(CVI_S8); i++) {
            rx_init_attr.as8PNSwap[i] = pstSnsCfg->as8PNSwap[i];
        }

        if (pfnSnsObj->pfnPatchRxAttr) {
            s32Ret = pfnSnsObj->pfnPatchRxAttr(&rx_init_attr);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnPatchRxAttr(%d) failed!\n", ViPipe);
        }

        s32Ret = app_ipcam_Isp_InitAttr_Get(pstSnsCfg->enSnsType, pstChnCfg->enWDRMode, &isp_init_attr);
        APP_IPCAM_CHECK_RET(s32Ret, "app_ipcam_Isp_InitAttr_Get(%d) failed!\n", ViPipe);

        isp_init_attr.u16UseHwSync = pstSnsCfg->bHwSync;
        if (pfnSnsObj->pfnSetInit) {
            s32Ret = pfnSnsObj->pfnSetInit(ViPipe, &isp_init_attr);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnSetInit(%d) failed!\n", ViPipe);
        }

        memset(&sns_bus_info, 0, sizeof(ISP_SNS_COMMBUS_U));
        sns_bus_info.s8I2cDev = (pstSnsCfg->s32BusId >= 0) ? (CVI_S8)pstSnsCfg->s32BusId : 0x3;
        if (pfnSnsObj->pfnSetBusInfo) {
            s32Ret = pfnSnsObj->pfnSetBusInfo(ViPipe, sns_bus_info);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnSetBusInfo(%d) failed!\n", ViPipe);
        }

        if (pfnSnsObj->pfnPatchI2cAddr) {
            pfnSnsObj->pfnPatchI2cAddr(pstSnsCfg->s32I2cAddr);
        }

        awb_lib.s32Id = ViPipe;
        ae_lib.s32Id = ViPipe;
        strcpy(ae_lib.acLibName, CVI_AE_LIB_NAME);//, sizeof(CVI_AE_LIB_NAME));
        strcpy(awb_lib.acLibName, CVI_AWB_LIB_NAME);//, sizeof(CVI_AWB_LIB_NAME));
        if (pfnSnsObj->pfnRegisterCallback) {
            s32Ret = pfnSnsObj->pfnRegisterCallback(ViPipe, &ae_lib, &awb_lib);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnRegisterCallback(%d) failed!\n", ViPipe);
        }

        memset(&isp_cmos_sensor_image_mode, 0, sizeof(ISP_CMOS_SENSOR_IMAGE_MODE_S));
        if(app_ipcam_Isp_PubAttr_Get(pstSnsCfg->enSnsType, &stPubAttr) != CVI_SUCCESS)
        {
            APP_PROF_LOG_PRINT(LEVEL_INFO, "Can't get sns attr\n");
            return CVI_FALSE;
        }
        isp_cmos_sensor_image_mode.u16Width  = pstChnCfg->u32Width;
        isp_cmos_sensor_image_mode.u16Height = pstChnCfg->u32Height;
        isp_cmos_sensor_image_mode.f32Fps    = stPubAttr.f32FrameRate;
        APP_PROF_LOG_PRINT(LEVEL_INFO, "sensor %d, Width %d, Height %d, FPS %f, wdrMode %d, pfnSnsObj %p\n",
                s32SnsId,
                isp_cmos_sensor_image_mode.u16Width, isp_cmos_sensor_image_mode.u16Height,
                isp_cmos_sensor_image_mode.f32Fps, pstChnCfg->enWDRMode,
                pfnSnsObj);

        if (pfnSnsObj->pfnExpSensorCb) {
            s32Ret = pfnSnsObj->pfnExpSensorCb(&isp_sensor_exp_func);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnExpSensorCb(%d) failed!\n", ViPipe);

            isp_sensor_exp_func.pfn_cmos_sensor_global_init(ViPipe);

            s32Ret = isp_sensor_exp_func.pfn_cmos_set_image_mode(ViPipe, &isp_cmos_sensor_image_mode);
            APP_IPCAM_CHECK_RET(s32Ret, "pfn_cmos_set_image_mode(%d) failed!\n", ViPipe);

            s32Ret = isp_sensor_exp_func.pfn_cmos_set_wdr_mode(ViPipe, pstChnCfg->enWDRMode);
            APP_IPCAM_CHECK_RET(s32Ret, "pfn_cmos_set_wdr_mode(%d) failed!\n", ViPipe);
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Mipi_Start(void)
{
    CVI_S32 s32Ret;
    VI_PIPE ViPipe;
    ISP_SNS_OBJ_S *pfnSnsObj = CVI_NULL;
    SNS_COMBO_DEV_ATTR_S combo_dev_attr;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe    = pstChnCfg->s32ChnId;

        pfnSnsObj = app_ipcam_SnsObj_Get(pstSnsCfg->enSnsType);
        if (pfnSnsObj == CVI_NULL) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"sensor obj(%d) is null\n", ViPipe);
            return CVI_FAILURE;
        }

        memset(&combo_dev_attr, 0, sizeof(SNS_COMBO_DEV_ATTR_S));
        if (pfnSnsObj->pfnGetRxAttr) {
            s32Ret = pfnSnsObj->pfnGetRxAttr(ViPipe, &combo_dev_attr);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnGetRxAttr(%d) failed!\n", ViPipe);
            pstSnsCfg->MipiDev = combo_dev_attr.devno;
            ViPipe = pstSnsCfg->MipiDev;
            APP_PROF_LOG_PRINT(LEVEL_INFO, "sensor %d devno %d\n", i, ViPipe);
        }

        s32Ret = CVI_MIPI_SetSensorReset(ViPipe, 1);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_MIPI_SetSensorReset(%d) failed!\n", ViPipe);

        s32Ret = CVI_MIPI_SetMipiReset(ViPipe, 1);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_MIPI_SetMipiReset(%d) failed!\n", ViPipe);

        if ((pstSnsCfg->enSnsType == SENSOR_VIVO_MCS369) ||
            (pstSnsCfg->enSnsType == SENSOR_VIVO_MCS369Q)) {
            CVI_MIPI_SetClkEdge(ViPipe, 0);
        }

        s32Ret = CVI_MIPI_SetMipiAttr(ViPipe, (CVI_VOID*)&combo_dev_attr);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_MIPI_SetMipiAttr(%d) failed!\n", ViPipe);

        s32Ret = CVI_MIPI_SetSensorClock(ViPipe, 1);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_MIPI_SetSensorClock(%d) failed!\n", ViPipe);

        usleep(20);
        s32Ret = CVI_MIPI_SetSensorReset(ViPipe, 0);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_MIPI_SetSensorReset(%d) failed!\n", ViPipe);

        if (pfnSnsObj->pfnSnsProbe) {
            s32Ret = pfnSnsObj->pfnSnsProbe(ViPipe);
            APP_IPCAM_CHECK_RET(s32Ret, "pfnSnsProbe(%d) failed!\n", ViPipe);
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Dev_Start(void)
{
    CVI_S32 s32Ret;

    VI_DEV         ViDev;
    VI_DEV_ATTR_S  stViDevAttr;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViDev = pstChnCfg->s32ChnId;
        app_ipcam_Vi_DevAttr_Get(pstSnsCfg->enSnsType, &stViDevAttr);

        stViDevAttr.stSize.u32Width     = pstChnCfg->u32Width;
        stViDevAttr.stSize.u32Height    = pstChnCfg->u32Height;
        stViDevAttr.stWDRAttr.enWDRMode = pstChnCfg->enWDRMode;
        s32Ret = CVI_VI_SetDevAttr(ViDev, &stViDevAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_VI_SetDevAttr(%d) failed!\n", ViDev);

        s32Ret = CVI_VI_EnableDev(ViDev);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_VI_EnableDev(%d) failed!\n", ViDev);
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Dev_Stop(void)
{
    CVI_S32 s32Ret;
    VI_DEV ViDev;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViDev = pstChnCfg->s32ChnId;
        s32Ret  = CVI_VI_DisableDev(ViDev);

        // CVI_VI_UnRegChnFlipMirrorCallBack(0, ViDev);
        // CVI_VI_UnRegPmCallBack(ViDev);
        // memset(&ViPmData[ViDev], 0, sizeof(struct VI_PM_DATA_S));

        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VI_DisableDev failed with %#x!\n", s32Ret);
            return s32Ret;
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Pipe_Start(void)
{
    CVI_S32 s32Ret;

    VI_PIPE        ViPipe;
    VI_PIPE_ATTR_S stViPipeAttr;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        APP_PARAM_PIPE_CFG_T *psPipeCfg = &g_pstViCtx->astPipeInfo[i];

        s32Ret = app_ipcam_Vi_PipeAttr_Get(pstSnsCfg->enSnsType, &stViPipeAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "app_ipcam_Vi_PipeAttr_Get failed!\n");

        stViPipeAttr.u32MaxW = pstChnCfg->u32Width;
        stViPipeAttr.u32MaxH = pstChnCfg->u32Height;
        stViPipeAttr.enCompressMode = pstChnCfg->enCompressMode;

        for (int j = 0; j < WDR_MAX_PIPE_NUM; j++) {
            if ((psPipeCfg->aPipe[j] >= 0) && (psPipeCfg->aPipe[j] < WDR_MAX_PIPE_NUM)) {
                ViPipe = psPipeCfg->aPipe[j];

                s32Ret = CVI_VI_CreatePipe(ViPipe, &stViPipeAttr);
                APP_IPCAM_CHECK_RET(s32Ret, "CVI_VI_CreatePipe(%d) failed!\n", ViPipe);

                s32Ret = CVI_VI_StartPipe(ViPipe);
                APP_IPCAM_CHECK_RET(s32Ret, "CVI_VI_StartPipe(%d) failed!\n", ViPipe);
            }
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Pipe_Stop(void)
{
    CVI_S32 s32Ret;
    VI_PIPE ViPipe;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_PIPE_CFG_T *psPipeCfg = &g_pstViCtx->astPipeInfo[i];
        for (int j = 0; j < WDR_MAX_PIPE_NUM; j++) {
            ViPipe = psPipeCfg->aPipe[j];
            s32Ret = CVI_VI_StopPipe(ViPipe);
            if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VI_StopPipe failed with %#x!\n", s32Ret);
                return s32Ret;
            }

            s32Ret = CVI_VI_DestroyPipe(ViPipe);
            if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VI_DestroyPipe failed with %#x!\n", s32Ret);
                return s32Ret;
            }
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Chn_Start(void)
{
    CVI_S32 s32Ret;

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    VI_DEV_ATTR_S  stViDevAttr;
    VI_CHN_ATTR_S  stViChnAttr;
    ISP_SNS_OBJ_S  *pstSnsObj = CVI_NULL;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_SNS_CFG_T *pstSnsCfg = &g_pstViCtx->astSensorCfg[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;
        ViChn = pstChnCfg->s32ChnId;

        pstSnsObj = app_ipcam_SnsObj_Get(pstSnsCfg->enSnsType);
        if (pstSnsObj == CVI_NULL) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "sensor obj(%d) is null\n", ViPipe);
            return CVI_FAILURE;
        }

        s32Ret = app_ipcam_Vi_DevAttr_Get(pstSnsCfg->enSnsType, &stViDevAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "app_ipcam_Vi_DevAttr_Get(%d) failed!\n", ViPipe);

        s32Ret = app_ipcam_Vi_ChnAttr_Get(pstSnsCfg->enSnsType, &stViChnAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "app_ipcam_Vi_ChnAttr_Get(%d) failed!\n", ViPipe);

        stViChnAttr.stSize.u32Width  = pstChnCfg->u32Width;
        stViChnAttr.stSize.u32Height = pstChnCfg->u32Height;
        stViChnAttr.enCompressMode = pstChnCfg->enCompressMode;
        stViChnAttr.enPixelFormat = pstChnCfg->enPixFormat;

        stViChnAttr.u32Depth         = 0; // depth
        // stViChnAttr.bLVDSflow        = (stViDevAttr.enIntfMode == VI_MODE_LVDS) ? 1 : 0;
        // stViChnAttr.u8TotalChnNum    = vt->ViConfig.s32WorkingViNum;

        /* fill the sensor orientation */
        if (pstSnsCfg->u8Orien <= 3) {
            stViChnAttr.bMirror = pstSnsCfg->u8Orien & 0x1;
            stViChnAttr.bFlip = pstSnsCfg->u8Orien & 0x2;
        }

        s32Ret = CVI_VI_SetChnAttr(ViPipe, ViChn, &stViChnAttr);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_VI_SetChnAttr(%d) failed!\n", ViPipe);

        if (pstSnsObj && pstSnsObj->pfnMirrorFlip) {
            CVI_VI_RegChnFlipMirrorCallBack(ViPipe, ViChn, (void *)pstSnsObj->pfnMirrorFlip);
        }

        s32Ret = CVI_VI_EnableChn(ViPipe, ViChn);
        APP_IPCAM_CHECK_RET(s32Ret, "CVI_VI_EnableChn(%d) failed!\n", ViPipe);

    }
    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Chn_Stop(void)
{
    CVI_S32 s32Ret;
    VI_CHN ViChn;
    VI_PIPE ViPipe;
    VI_VPSS_MODE_E enMastPipeMode;

    for (CVI_U32 i = 0; i < g_pstViCtx->u32WorkSnsCnt; i++) {
        APP_PARAM_PIPE_CFG_T *psPipeCfg = &g_pstViCtx->astPipeInfo[i];
        APP_PARAM_CHN_CFG_T *pstChnCfg = &g_pstViCtx->astChnInfo[i];
        ViPipe = pstChnCfg->s32ChnId;
        ViChn = pstChnCfg->s32ChnId;
        enMastPipeMode = psPipeCfg->enMastPipeMode;

        if (ViChn < VI_MAX_CHN_NUM) {
            CVI_VI_UnRegChnFlipMirrorCallBack(ViPipe, ViChn);

            if (enMastPipeMode == VI_OFFLINE_VPSS_OFFLINE || enMastPipeMode == VI_ONLINE_VPSS_OFFLINE) {
                s32Ret = CVI_VI_DisableChn(ViPipe, ViChn);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VI_DisableChn failed with %#x!\n", s32Ret);
                    return s32Ret;
                }
            }
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vi_Close(void)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    s32Ret = CVI_SYS_VI_Close();
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_VI_Close failed with %#x!\n", s32Ret);
    }

    return s32Ret;
}

int app_ipcam_Vi_DeInit(void)
{
    APP_CHK_RET(app_ipcam_Vi_Isp_Stop(),  "app_ipcam_Vi_Isp_Stop");
    APP_CHK_RET(app_ipcam_Vi_Isp_DeInit(),  "app_ipcam_Vi_Isp_DeInit");
    APP_CHK_RET(app_ipcam_Vi_Chn_Stop(),  "app_ipcam_Vi_Chn_Stop");
    APP_CHK_RET(app_ipcam_Vi_Pipe_Stop(), "app_ipcam_Vi_Pipe_Stop");
    APP_CHK_RET(app_ipcam_Vi_Dev_Stop(),  "app_ipcam_Vi_Dev_Stop");
    APP_CHK_RET(app_ipcam_Vi_Close(),  "app_ipcam_Vi_Close");

    return CVI_SUCCESS;
}

int app_ipcam_Vi_Init(void)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    s32Ret = CVI_SYS_VI_Open();
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_VI_Open failed with %#x\n", s32Ret);
        return s32Ret;
    }

    s32Ret = app_ipcam_Vi_Sensor_Start();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Vi_Sensor_Start failed with %#x\n", s32Ret);
        goto VI_EXIT0;
    }

    s32Ret = app_ipcam_Vi_Mipi_Start();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Mipi_Start failed with %#x\n", s32Ret);
        goto VI_EXIT0;
    }

    s32Ret = app_ipcam_Vi_Dev_Start();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Dev_Start failed with %#x\n", s32Ret);
        goto VI_EXIT0;
    }

    s32Ret = app_ipcam_Vi_Pipe_Start();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Pipe_Start failed with %#x\n", s32Ret);
        goto VI_EXIT1;
    }

    s32Ret = app_ipcam_Vi_Isp_Init();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Isp_Init failed with %#x\n", s32Ret);
        goto VI_EXIT2;
    }

    s32Ret = app_ipcam_Vi_Isp_Start();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Isp_Start failed with %#x\n", s32Ret);
        goto VI_EXIT3;
    }

    s32Ret = app_ipcam_Vi_Chn_Start();
    if(s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "app_ipcam_Chn_Start failed with %#x\n", s32Ret);
        goto VI_EXIT4;
    }

    return CVI_SUCCESS;

VI_EXIT4:
    app_ipcam_Vi_Isp_Stop();

VI_EXIT3:
    app_ipcam_Vi_Isp_DeInit();

VI_EXIT2:
    app_ipcam_Vi_Pipe_Stop();

VI_EXIT1:
    app_ipcam_Vi_Dev_Stop();

VI_EXIT0:
    app_ipcam_Vi_Close();
    app_ipcam_Sys_DeInit();

    return s32Ret;
}



// video_paramparse.c

// SYS - vb_pool
static const APP_PARAM_VB_CFG_S vbpool = {
    .bEnable = 0,
    .width = 1,//1920,
    .height = 1,//1080,
    .fmt = PIXEL_FORMAT_NV21,
    .enBitWidth = DATA_BITWIDTH_8,
    .enCmpMode = COMPRESS_MODE_NONE,
    .vb_blk_num = 2,
};

// VI
static const APP_PARAM_SNS_CFG_T sns_cfg_ov5647 = {
    .s32SnsId = 0,
    .enSnsType = SENSOR_OV_OV5647,
    .s32Framerate = 30,
    .s32BusId = 2,
    .s32I2cAddr = 36,
    .MipiDev = 0,
    .as16LaneId = { 2, 0, 3, -1, -1 },
    .as8PNSwap = { 0, 0, 0, 0, 0 },
    .bMclkEn = 1,
    .u8Mclk = 0,
    .u8Orien = 1,
    .bHwSync = 0,
    .u8UseDualSns = 0,
};

static const APP_PARAM_SNS_CFG_T sns_cfg_sc530ai = {
    .s32SnsId = 0,
    .enSnsType = SENSOR_SMS_SC530AI_2L,
    .s32Framerate = 30,
    .s32BusId = 2,
    .s32I2cAddr = 30,
    .MipiDev = 0,
    .as16LaneId = { 2, 0, 3, -1, -1 },
    .as8PNSwap = { 0, 0, 0, 0, 0 },
    .bMclkEn = 1,
    .u8Mclk = 0,
    .u8Orien = ISP_SNS_MIRROR_FLIP,
    .bHwSync = 0,
    .u8UseDualSns = 0,
};

static const APP_PARAM_DEV_CFG_T dev_cfg = {
    .ViDev = 0,
    .enWDRMode = WDR_MODE_NONE,
};

static const APP_PARAM_PIPE_CFG_T pipe_cfg = {
    .aPipe = { 0, -1, -1, -1 },
    .enMastPipeMode = VI_OFFLINE_VPSS_OFFLINE,
};

static const APP_PARAM_CHN_CFG_T chn_cfg = {
    .s32ChnId = 0,
    .u32Width = 1920,
    .u32Height = 1080,
    .f32Fps = -1,
    .enPixFormat = PIXEL_FORMAT_NV21,
    .enDynamicRange = DYNAMIC_RANGE_SDR8,
    .enVideoFormat = VIDEO_FORMAT_LINEAR,
    .enCompressMode = COMPRESS_MODE_TILE,
};

// VPSS
static const VPSS_CHN_ATTR_S chn_attr = {
    .u32Width = 1920,
    .u32Height = 1080,
    .enVideoFormat = VIDEO_FORMAT_LINEAR,
    .enPixelFormat = PIXEL_FORMAT_NV21,
    .stFrameRate = {
        .s32SrcFrameRate = -1,
        .s32DstFrameRate = -1,
    },
    .bMirror = 0,
    .bFlip = 0,
    .u32Depth = 0,
    .stAspectRatio = {
        .enMode = ASPECT_RATIO_AUTO,
        .bEnableBgColor = 1,
        .u32BgColor = 0x727272,
    },
};

static const VPSS_GRP_ATTR_S grp_attr = {
    .u32MaxW = 1920,
    .u32MaxH = 1080,
    .enPixelFormat = PIXEL_FORMAT_NV21,
    .stFrameRate = {
        .s32SrcFrameRate = -1,
        .s32DstFrameRate = -1,
    },
    .u8VpssDev = 1,
};


static const APP_VPSS_GRP_CFG_T vpss_grp = {
    .VpssGrp = 0,
    .bEnable = 1,
    .stVpssGrpCropInfo = {
        .bEnable = 0,
    },
    .bBindMode = 0,
};

// VENC - H264
static const APP_GOP_PARAM_U h264_stNormalP = {
    .stNormalP = {
        .s32IPQpDelta = 2,
    },
};

static const APP_RC_PARAM_S h264_stRcParam = {
    .s32FirstFrameStartQp = 35,
    .s32InitialDelay = 1000,
    .u32ThrdLv = 2,
    .s32ChangePos = 75,
    .u32MinIprop = 1,
    .u32MaxIprop = 10,
    .s32MaxReEncodeTimes = 0,
    .s32MinStillPercent = 10,
    .u32MaxStillQP = 38,
    .u32MinStillPSNR = 0,
    .u32MaxQp = 35,
    .u32MinQp = 20,
    .u32MaxIQp = 35,
    .u32MinIQp = 20,
    .u32MotionSensitivity = 24,
    .s32AvbrFrmLostOpen = 0,
    .s32AvbrFrmGap = 1,
    .s32AvbrPureStillThr = 4,
};

static const APP_VENC_CHN_CFG_S venc_h264 = {
    .bEnable = 0,
    .enType = PT_H264,
    .u32Duration = 75,
    .u32Width = 1920,
    .u32Height = 1080,
    .u32SrcFrameRate = (CVI_U32)-1,
    .u32DstFrameRate = (CVI_U32)-1,
    .u32BitRate = 1000,
    .u32MaxBitRate = 1000,
    .u32StreamBufSize = (512 << 10),
    .VpssGrp = 0,
    .VpssChn = 0,
    .u32Profile = 0,
    .bSingleCore = 0,
    .u32Gop = 50,
    .u32IQp = 38,
    .u32PQp = 38,
    .statTime = 2,
    .enBindMode = VENC_BIND_VPSS,
    .astChn = {
    {
        // src
        .enModId = CVI_ID_VPSS,
        .s32DevId = 0,
        .s32ChnId = 0,
    },
    {
        // dst
        .enModId = CVI_ID_VENC,
        .s32DevId = 0,
        .s32ChnId = 0,
    }},
    .enGopMode = VENC_GOPMODE_NORMALP,
    .unGopParam = h264_stNormalP,
    .enRcMode = VENC_RC_MODE_H264CBR,
    .stRcParam = h264_stRcParam,
};

// VENC - H265
static const APP_GOP_PARAM_U h265_stNormalP = {
    .stNormalP = {
        .s32IPQpDelta = 2,
    },
};

static const APP_RC_PARAM_S h265_stRcParam = {
    .s32FirstFrameStartQp = 35,
    .s32InitialDelay = 1000,
    .u32ThrdLv = 2,
    .s32ChangePos = 75,
    .u32MinIprop = 1,
    .u32MaxIprop = 100,
    .s32MaxReEncodeTimes = 0,
    .s32MinStillPercent = 10,
    .u32MaxStillQP = 33,
    .u32MinStillPSNR = 0,
    .u32MaxQp = 35,
    .u32MinQp = 20,
    .u32MaxIQp = 35,
    .u32MinIQp = 20,
    .u32MotionSensitivity = 24,
    .s32AvbrFrmLostOpen = 0,
    .s32AvbrFrmGap = 1,
    .s32AvbrPureStillThr = 4,
};

static const APP_VENC_CHN_CFG_S venc_h265 = {
    .bEnable = 0,
    .enType = PT_H265,
    .u32Duration = 75,
    .u32Width = 1920,
    .u32Height = 1080,
    .u32SrcFrameRate = (CVI_U32)-1,
    .u32DstFrameRate = (CVI_U32)-1,
    .u32BitRate = 3000,
    .u32MaxBitRate = 3000,
    .u32StreamBufSize = (1024 << 10),
    .VpssGrp = 0,
    .VpssChn = 0,
    .u32Profile = 0,
    .bSingleCore = 0,
    .u32Gop = 50,
    .u32IQp = 38,
    .u32PQp = 38,
    .statTime = 2,
    .enBindMode = VENC_BIND_VPSS,
    .astChn = {
    {
        // src
        .enModId = CVI_ID_VPSS,
        .s32DevId = 0,
        .s32ChnId = 0,
    },
    {
        // dst
        .enModId = CVI_ID_VENC,
        .s32DevId = 0,
        .s32ChnId = 0,
    }},
    .enGopMode = VENC_GOPMODE_NORMALP,
    .unGopParam = h265_stNormalP,
    .enRcMode = VENC_RC_MODE_H265CBR,
    .stRcParam = h265_stRcParam,
};



static const APP_VENC_CHN_CFG_S venc_jpeg = {
    .bEnable = 0,
    .enType = PT_JPEG,
    .u32Duration = 0,
    .u32Width = 1920,
    .u32Height = 1080,
    .u32StreamBufSize = (512 << 10),
    .VpssGrp = 0,
    .VpssChn = 0,
    .enBindMode = VENC_BIND_DISABLE,
    .enRcMode = VENC_RC_MODE_MJPEGCBR,
    .stJpegCodecParam = {
        .quality = 60,
        .MCUPerECS = 0,
    },
};

static const APP_VENC_ROI_CFG_S roi_cfg = { 0 };

int app_ipcam_Param_setVencChnType(int ch, PAYLOAD_TYPE_E enType)
{
    APP_PARAM_VENC_CTX_S* venc = app_ipcam_Venc_Param_Get();

    if (ch >= venc->s32VencChnCnt) {
        return -1;
    }

    APP_VENC_CHN_CFG_S* pvchn = &venc->astVencChnCfg[ch];
    if (enType == PT_H264) {
        *pvchn = venc_h264;
        pvchn->enGopMode = VENC_GOPMODE_NORMALP;
        pvchn->unGopParam = h264_stNormalP;
        pvchn->enRcMode = VENC_RC_MODE_H264CBR;
        pvchn->stRcParam = h264_stRcParam;
    } else if (enType == PT_H265) {
        *pvchn = venc_h265;
        pvchn->enGopMode = VENC_GOPMODE_NORMALP;
        pvchn->unGopParam = h265_stNormalP;
        pvchn->enRcMode = VENC_RC_MODE_H265CBR;
        pvchn->stRcParam = h265_stRcParam;
    } else if (enType == PT_JPEG) {
        *pvchn = venc_jpeg;
    }
    pvchn->VencChn = ch;
    pvchn->VpssGrp = 0;
    pvchn->VpssChn = ch;
    pvchn->astChn[0].enModId = CVI_ID_VPSS;
    pvchn->astChn[0].s32DevId = 0; // src
    pvchn->astChn[0].s32ChnId = ch;
    pvchn->astChn[1].enModId = CVI_ID_VENC;
    pvchn->astChn[1].s32DevId = 0; // dst
    pvchn->astChn[1].s32ChnId = ch;

    return CVI_SUCCESS;
}

static const APP_PARAM_SNS_CFG_T* supported_sensors[] = { &sns_cfg_ov5647, &sns_cfg_sc530ai };
static const APP_PARAM_SNS_CFG_T* vi_sensor_identify(void)
{
    VI_PIPE ViPipe = 0;
    CVI_S32 s32Ret = CVI_SUCCESS;
    const APP_PARAM_SNS_CFG_T* pstSnsCfg = &sns_cfg_ov5647;
    ISP_SNS_OBJ_S* pfnSnsObj = CVI_NULL;

    // Since we're only using OV5647, simplify the sensor identification
        pfnSnsObj = app_ipcam_SnsObj_Get(pstSnsCfg->enSnsType);
        if (CVI_NULL == pfnSnsObj) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "Failed to get sensor object for OV5647\n");
        return CVI_NULL;
        }

        ISP_SNS_COMMBUS_U sns_bus_info;
        memset(&sns_bus_info, 0, sizeof(ISP_SNS_COMMBUS_U));
        sns_bus_info.s8I2cDev = (pstSnsCfg->s32BusId >= 0) ? (CVI_S8)pstSnsCfg->s32BusId : 0x3;
    
        if (pfnSnsObj->pfnSetBusInfo) {
            s32Ret = pfnSnsObj->pfnSetBusInfo(ViPipe, sns_bus_info);
            if (CVI_SUCCESS != s32Ret) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "Failed to set bus info for OV5647\n");
                return CVI_NULL;
            }
        }

        if (pfnSnsObj->pfnPatchI2cAddr) {
            pfnSnsObj->pfnPatchI2cAddr(pstSnsCfg->s32I2cAddr);
        }

        if (pfnSnsObj->pfnSnsProbe) {
            s32Ret = pfnSnsObj->pfnSnsProbe(ViPipe);
            if (s32Ret == CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_INFO, "OV5647 sensor detected\n");
                return pstSnsCfg;
        }
    }

    APP_PROF_LOG_PRINT(LEVEL_ERROR, "OV5647 sensor not found\n");
    return CVI_NULL;
}

char* app_ipcam_Isp_pq_bin(void)
{
    static char pq_bin[64] = PQ_BIN_SDR_PREFIX;

    APP_PARAM_VI_CTX_S* vi = app_ipcam_Vi_Param_Get();

    if (vi->astSensorCfg[0].enSnsType == SENSOR_SMS_SC530AI_2L) {
        sprintf(pq_bin, PQ_BIN_SDR_PREFIX"_sc530ai_2l");
    }

    return pq_bin;
}

static void fix_vi_chn_cfg(const APP_PARAM_SNS_CFG_T* pstSnsCfg, APP_PARAM_CHN_CFG_T* chn_cfg)
{
    if (SENSOR_SMS_SC530AI_2L == pstSnsCfg->enSnsType) {
        chn_cfg->u32Width = 2880;
        chn_cfg->u32Height = 1620;
    }
}

static void fix_vi_grp_attr(const APP_PARAM_SNS_CFG_T* pstSnsCfg, VPSS_GRP_ATTR_S* grp_attr)
{
    if (SENSOR_SMS_SC530AI_2L == pstSnsCfg->enSnsType) {
        grp_attr->u32MaxW = 2880;
        grp_attr->u32MaxH = 1620;
    }
}

#define APP_IPCAM_CHN_NUM 3

int app_ipcam_Param_Load(bool use_venc)
{
    // sys
    APP_PARAM_SYS_CFG_S* sys = app_ipcam_Sys_Param_Get();
    sys->vb_pool_num = APP_IPCAM_CHN_NUM;
    for (uint32_t i = 0; i < sys->vb_pool_num; i++) {
        sys->vb_pool[i] = vbpool;
    }
    sys->stVIVPSSMode.aenMode[0] = VI_OFFLINE_VPSS_ONLINE;
    sys->stVPSSMode.enMode = VPSS_MODE_SINGLE;
    sys->stVPSSMode.aenInput[0] = VPSS_INPUT_ISP;//VPSS_INPUT_MEM;
    sys->stVPSSMode.aenInput[1] = VPSS_INPUT_ISP;
    sys->stVPSSMode.ViPipe[0] = 0;
    sys->stVPSSMode.ViPipe[1] = 0;
    sys->bSBMEnable = 0;

    // vi
    const APP_PARAM_SNS_CFG_T* pstSnsCfg = vi_sensor_identify();
    if (CVI_NULL == pstSnsCfg) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "vi sensor not found\n");
        return CVI_ERR_VI_INVALID_DEVID;
    }
    APP_PARAM_VI_CTX_S* vi = app_ipcam_Vi_Param_Get();
    vi->u32WorkSnsCnt = 1;
    vi->astSensorCfg[0] = *pstSnsCfg;
    vi->astSensorCfg[0].s32SnsId = 0;
    vi->astDevInfo[0] = dev_cfg;
    vi->astDevInfo[0].ViDev = 0;
    vi->astPipeInfo[0] = pipe_cfg;
    vi->astChnInfo[0] = chn_cfg;
    vi->astChnInfo[0].s32ChnId = 0;
    vi->astIspCfg[0].bAfFliter = 0;
    fix_vi_chn_cfg(pstSnsCfg, &vi->astChnInfo[0]);

    // vpss
    APP_PARAM_VPSS_CFG_T* vpss = app_ipcam_Vpss_Param_Get();
    vpss->u32GrpCnt = 1;
    vpss->astVpssGrpCfg[0] = vpss_grp;
    APP_VPSS_GRP_CFG_T* pgrp = &vpss->astVpssGrpCfg[0];
    pgrp->VpssGrp = 0;
    pgrp->stVpssGrpAttr = grp_attr;
    fix_vi_grp_attr(pstSnsCfg, &pgrp->stVpssGrpAttr);
    pgrp->stVpssGrpAttr.u8VpssDev = 0;
    pgrp->bBindMode = 0;
    pgrp->astChn[0].enModId = CVI_ID_VI; // src
    pgrp->astChn[0].s32DevId = 0;
    pgrp->astChn[0].s32ChnId = 0;
    pgrp->astChn[1].enModId = CVI_ID_VPSS; // src
    pgrp->astChn[1].s32DevId = 0;
    pgrp->astChn[1].s32ChnId = 0;
    for (uint32_t i = 0; i < APP_IPCAM_CHN_NUM; i++) {
        pgrp->abChnEnable[i] = 0; // default disabled
        pgrp->aAttachEn[i] = 0;
        pgrp->aAttachPool[i] = i;
        pgrp->astVpssChnAttr[i] = chn_attr;
    }

    // venc
    if (use_venc) {  // TODO: remove this?
        APP_PARAM_VENC_CTX_S* venc = app_ipcam_Venc_Param_Get();
        venc->s32VencChnCnt = APP_IPCAM_CHN_NUM;
        for (uint32_t i = 0; i < venc->s32VencChnCnt; i++) {
            app_ipcam_Param_setVencChnType(i, PT_H264);
        }
    }

    return CVI_SUCCESS;
}


// video_sys.c



/**************************************************************************
 *                              M A C R O S                               *
 **************************************************************************/

/**************************************************************************
 *                           C O N S T A N T S                            *
 **************************************************************************/

/**************************************************************************
 *                          D A T A    T Y P E S                          *
 **************************************************************************/

/**************************************************************************
 *                         G L O B A L    D A T A                         *
 **************************************************************************/
APP_PARAM_SYS_CFG_S g_stSysAttrCfg, *g_pstSysAttrCfg = &g_stSysAttrCfg;

/**************************************************************************
 *                 E X T E R N A L    R E F E R E N C E S                 *
 **************************************************************************/

/**************************************************************************
 *               F U N C T I O N    D E C L A R A T I O N S               *
 **************************************************************************/
APP_PARAM_SYS_CFG_S *app_ipcam_Sys_Param_Get(void)
{
    return g_pstSysAttrCfg;
}

static int COMM_SYS_Init(VB_CONFIG_S *pstVbConfig)
{
    CVI_S32 s32Ret = CVI_FAILURE;

    CVI_SYS_Exit();
    CVI_VB_Exit();

    if (pstVbConfig == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "input parameter is null, it is invaild!\n");
        return APP_IPCAM_ERR_FAILURE;
    }

    s32Ret = CVI_VB_SetConfig(pstVbConfig);
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_VB_SetConf failed!\n");
        return s32Ret;
    }

    s32Ret = CVI_VB_Init();
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_VB_Init failed!\n");
        return s32Ret;
    }

    s32Ret = CVI_SYS_Init();
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_SYS_Init failed!\n");
        CVI_VB_Exit();
        return s32Ret;
    }

    #ifdef FAST_BOOT_ENABLE
    s32Ret = CVI_EFUSE_EnableFastBoot();
    if (s32Ret < 0) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_EFUSE_EnableFastBoot failed with %#x\n", s32Ret);
        return CVI_FAILURE;
    }

    s32Ret = CVI_EFUSE_IsFastBootEnabled();
    if (s32Ret < 0) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_EFUSE_IsFastBootEnabled failed with %#x\n", s32Ret);
        return CVI_FAILURE;
    }

    CVI_TRACE_LOG(CVI_DBG_INFO, "cv182x enable fastboot done\n");
    #endif

    return CVI_SUCCESS;
}

static uint32_t get_frame_size(
        uint32_t w,
        uint32_t h,
        PIXEL_FORMAT_E fmt,
        DATA_BITWIDTH_E enBitWidth,
        COMPRESS_MODE_E enCmpMode)
{
    // try rotate and non-rotate, choose the larger one

    uint32_t size_w_h = COMMON_GetPicBufferSize(w, h, fmt,
                            enBitWidth, enCmpMode, DEFAULT_ALIGN);

    #if 0
    uint32_t size_h_w = COMMON_GetPicBufferSize(h, w, fmt,
        enBitWidth, enCmpMode, DEFAULT_ALIGN);
    return (size_w_h > size_h_w) ? size_w_h : size_h_w;
    #else
    return size_w_h;
    #endif
}

int app_ipcam_Sys_DeInit(void)
{
    APP_CHK_RET(CVI_VB_Exit()," Vb Exit");
    APP_CHK_RET(CVI_SYS_Exit()," Systerm Exit");

    return CVI_SUCCESS;
}

/// pass resolution and blkcnt directly for now
/// and assuming yuv420 for now
/// TODO: refactor to attribute struct
int app_ipcam_Sys_Init(void)
{
    APP_PROF_LOG_PRINT(LEVEL_INFO, "system init ------------------> start \n");

    int ret = CVI_SUCCESS;

    ret = CVI_VI_SetDevNum(app_ipcam_Vi_Param_Get()->u32WorkSnsCnt);
    if (ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VI_SetDevNum_%d failed with %#x\n", app_ipcam_Vi_Param_Get()->u32WorkSnsCnt, ret);
        app_ipcam_Sys_DeInit();
        return ret;
    }
    APP_PARAM_SYS_CFG_S *pattr = app_ipcam_Sys_Param_Get();

    //struct sigaction sa;
    //memset(&sa, 0, sizeof(struct sigaction));
    //sigemptyset(&sa.sa_mask);
    //sa.sa_sigaction = _SYS_HandleSig;
    //sa.sa_flags = SA_SIGINFO|SA_RESETHAND;    // Reset signal handler to system default after signal triggered
    //sigaction(SIGINT, &sa, NULL);
    //sigaction(SIGTERM, &sa, NULL);

    VB_CONFIG_S      stVbConf;
    memset(&stVbConf, 0, sizeof(VB_CONFIG_S));

    for (unsigned i = 0; i < pattr->vb_pool_num; i++) {
        uint32_t blk_size = get_frame_size(
                    pattr->vb_pool[i].width,
                    pattr->vb_pool[i].height,
                    pattr->vb_pool[i].fmt,
                    pattr->vb_pool[i].enBitWidth,
                    pattr->vb_pool[i].enCmpMode);

        uint32_t blk_num = pattr->vb_pool[i].vb_blk_num;

        stVbConf.astCommPool[i].u32BlkSize    = blk_size;
        stVbConf.astCommPool[i].u32BlkCnt     = blk_num;
        stVbConf.astCommPool[i].enRemapMode   = VB_REMAP_MODE_CACHED;

        stVbConf.u32MaxPoolCnt++;
        APP_PROF_LOG_PRINT(LEVEL_INFO, "VB pool[%d] BlkSize %d BlkCnt %d\n", i, blk_size, blk_num);
    }

    CVI_S32 rc = COMM_SYS_Init(&stVbConf);
    if (rc != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "COMM_SYS_Init fail, rc = %#x\n", rc);
        ret = APP_IPCAM_ERR_FAILURE;
        app_ipcam_Sys_DeInit();
        return ret;
    }

    rc = CVI_SYS_SetVIVPSSMode(&pattr->stVIVPSSMode);
    if (rc != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_SYS_SetVIVPSSMode failed with %#x\n", rc);
        ret = APP_IPCAM_ERR_FAILURE;
        app_ipcam_Sys_DeInit();
        return ret;
    }

    rc = CVI_SYS_SetVPSSModeEx(&pattr->stVPSSMode);
    if (rc != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_SYS_SetVPSSModeEx failed with %#x\n", rc);
        ret = APP_IPCAM_ERR_FAILURE;
        app_ipcam_Sys_DeInit();
        return ret;
    }

    APP_PROF_LOG_PRINT(LEVEL_INFO, "system init ------------------> done \n");
    return ret;
}


// vpss.c

/**************************************************************************
 *                              M A C R O S                               *
 **************************************************************************/

/**************************************************************************
 *                           C O N S T A N T S                            *
 **************************************************************************/

/**************************************************************************
 *                          D A T A    T Y P E S                          *
 **************************************************************************/

/**************************************************************************
 *                         G L O B A L    D A T A                         *
 **************************************************************************/
static APP_PARAM_VPSS_CFG_T g_stVpssCfg;
static APP_PARAM_VPSS_CFG_T *g_pstVpssCfg = &g_stVpssCfg;

/**************************************************************************
 *                 E X T E R N A L    R E F E R E N C E S                 *
 **************************************************************************/

/**************************************************************************
 *               F U N C T I O N    D E C L A R A T I O N S               *
 **************************************************************************/

APP_PARAM_VPSS_CFG_T *app_ipcam_Vpss_Param_Get(void)
{
    return g_pstVpssCfg;
}

static int app_ipcam_Vpss_Destroy(VPSS_GRP VpssGrp)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    APP_VPSS_GRP_CFG_T *pstVpssGrpCfg = &g_pstVpssCfg->astVpssGrpCfg[VpssGrp];
    if (!pstVpssGrpCfg->bCreate) {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "VpssGrp(%d) not create yet!\n", VpssGrp);
        return CVI_SUCCESS;
    }

    for (VPSS_CHN VpssChn = 0; VpssChn < VPSS_MAX_PHY_CHN_NUM; VpssChn++) {
        if (pstVpssGrpCfg->abChnCreate[VpssChn]) {
            s32Ret = CVI_VPSS_DisableChn(VpssGrp, VpssChn);
            if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_DisableChn failed with %#x!\n", s32Ret);
                return CVI_FAILURE;
            }
            pstVpssGrpCfg->abChnCreate[VpssChn] = CVI_FALSE;
        }
    }

    s32Ret = CVI_VPSS_StopGrp(VpssGrp);
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_StopGrp failed with %#x!\n", s32Ret);
        return CVI_FAILURE;
    }

    s32Ret = CVI_VPSS_DestroyGrp(VpssGrp);
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_DestroyGrp failed with %#x!\n", s32Ret);
        return CVI_FAILURE;
    }

    pstVpssGrpCfg->bCreate = CVI_FALSE;

    return CVI_SUCCESS;
}

static int app_ipcam_Vpss_Create(VPSS_GRP VpssGrp)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    APP_PARAM_SYS_CFG_S *pstSysCfg = app_ipcam_Sys_Param_Get();
    CVI_BOOL bSBMEnable = pstSysCfg->bSBMEnable;

    APP_VPSS_GRP_CFG_T *pstVpssGrpCfg = &g_pstVpssCfg->astVpssGrpCfg[VpssGrp];
    if (!pstVpssGrpCfg->bEnable) {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "VpssGrp(%d) not Enable!\n", VpssGrp);
        return CVI_SUCCESS;
    }

    if (pstVpssGrpCfg->bCreate) {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "VpssGrp(%d) have been created!\n", VpssGrp);
        return CVI_SUCCESS;
    }

    APP_PROF_LOG_PRINT(LEVEL_INFO, "GrpID=%d isEnable=%d\n", pstVpssGrpCfg->VpssGrp, pstVpssGrpCfg->bEnable);

    if ((s32Ret = CVI_VPSS_CreateGrp(pstVpssGrpCfg->VpssGrp, &pstVpssGrpCfg->stVpssGrpAttr)) != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_CreateGrp(grp:%d) failed with %d!\n", pstVpssGrpCfg->VpssGrp, s32Ret);
        goto VPSS_EXIT;
    }

    if ((s32Ret = CVI_VPSS_ResetGrp(pstVpssGrpCfg->VpssGrp)) != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_ResetGrp(grp:%d) failed with %d!\n", pstVpssGrpCfg->VpssGrp, s32Ret);
        goto VPSS_EXIT;
    }

    // Vpss Group not support crop if online
    // APP_PARAM_SYS_CFG_S *pstSysCfg = app_ipcam_Sys_Param_Get();
    // VI_VPSS_MODE_E vi_vpss_mode = pstSysCfg->stVIVPSSMode.aenMode[0];
    // if (vi_vpss_mode == VI_OFFLINE_VPSS_OFFLINE || vi_vpss_mode == VI_ONLINE_VPSS_OFFLINE) {
    //     if (CVI_VPSS_SetGrpCrop(pstVpssGrpCfg->VpssGrp, &pstVpssGrpCfg->stVpssGrpCropInfo) != CVI_SUCCESS) {
    //         APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_SetGrpCrop failed with %d\n", s32Ret);
    //         goto VPSS_EXIT;
    //     }
    // }

    for (VPSS_CHN VpssChn = 0; VpssChn < VPSS_MAX_PHY_CHN_NUM; VpssChn++) {
        APP_PROF_LOG_PRINT(LEVEL_INFO, "\tChnID=%d isEnable=%d\n", VpssChn, pstVpssGrpCfg->abChnEnable[VpssChn]);
        if (pstVpssGrpCfg->abChnEnable[VpssChn]) {

            if ((s32Ret = CVI_VPSS_SetChnAttr(pstVpssGrpCfg->VpssGrp, VpssChn, &pstVpssGrpCfg->astVpssChnAttr[VpssChn])) != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_SetChnAttr(%d) failed with %d\n", VpssChn, s32Ret);
                goto VPSS_EXIT;
            }

            if (CVI_VPSS_SetChnCrop(pstVpssGrpCfg->VpssGrp, VpssChn, &pstVpssGrpCfg->stVpssChnCropInfo[VpssChn]) != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_SetChnCrop(%d) failed with %d\n", VpssChn, s32Ret);
                goto VPSS_EXIT;
            }

            if ((bSBMEnable) && (VpssGrp == 0) && (VpssChn == 0)) {
                APP_PROF_LOG_PRINT(LEVEL_INFO, "CVI_VPSS_SetChnBufWrapAttr grp(%d) chn(%d) \n", VpssGrp, VpssChn);
                VPSS_CHN_BUF_WRAP_S stVpssChnBufWrap = {0};
                stVpssChnBufWrap.bEnable = CVI_TRUE;
                stVpssChnBufWrap.u32BufLine = 64;
                stVpssChnBufWrap.u32WrapBufferSize = 5;
                s32Ret = CVI_VPSS_SetChnBufWrapAttr(0, 0, &stVpssChnBufWrap);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_SetChnBufWrapAttr failed with %d\n", s32Ret);
                    goto VPSS_EXIT;
                }
            }

            if (CVI_VPSS_EnableChn(pstVpssGrpCfg->VpssGrp, VpssChn) != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_EnableChn(%d) failed with %d\n", VpssChn, s32Ret);
                goto VPSS_EXIT;
            }

            pstVpssGrpCfg->abChnCreate[VpssChn] = CVI_TRUE;

            if (pstVpssGrpCfg->aAttachEn[VpssChn]) {
                if (CVI_VPSS_AttachVbPool(pstVpssGrpCfg->VpssGrp, VpssChn, pstVpssGrpCfg->aAttachPool[VpssChn]) != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VPSS_AttachVbPool failed with %d\n", s32Ret);
                    goto VPSS_EXIT;
                }
            }
        }
    }

    s32Ret = CVI_VPSS_StartGrp(pstVpssGrpCfg->VpssGrp);
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "start vpss group failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    pstVpssGrpCfg->bCreate = CVI_TRUE;

    return CVI_SUCCESS;

VPSS_EXIT:
    app_ipcam_Vpss_DeInit();
    app_ipcam_Vi_DeInit();
    app_ipcam_Sys_DeInit();

    return s32Ret;

}

static int app_ipcam_Vpss_Unbind(VPSS_GRP VpssGrp)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    APP_VPSS_GRP_CFG_T *pstVpssGrpCfg = &g_pstVpssCfg->astVpssGrpCfg[VpssGrp];
    if (!pstVpssGrpCfg->bEnable) {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "VpssGrp(%d) not Enable!\n", VpssGrp);
        return CVI_SUCCESS;
    }

    APP_PROF_LOG_PRINT(LEVEL_INFO, "VpssGrp=%d bindMode:%d\n", VpssGrp, pstVpssGrpCfg->bBindMode);

    if (pstVpssGrpCfg->bBindMode) {
        s32Ret = CVI_SYS_UnBind(&pstVpssGrpCfg->astChn[0], &pstVpssGrpCfg->astChn[1]);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_UnBind failed with %#x\n", s32Ret);
            return s32Ret;
        }
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Vpss_Bind(VPSS_GRP VpssGrp)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    APP_VPSS_GRP_CFG_T *pstVpssGrpCfg = &g_pstVpssCfg->astVpssGrpCfg[VpssGrp];
    if (!pstVpssGrpCfg->bEnable) {
        APP_PROF_LOG_PRINT(LEVEL_WARN, "VpssGrp(%d) not Enable!\n", VpssGrp);
        return CVI_SUCCESS;
    }

    APP_PROF_LOG_PRINT(LEVEL_INFO, "GrpID=%d isEnable=%d\n", pstVpssGrpCfg->VpssGrp, pstVpssGrpCfg->bEnable);

    if (pstVpssGrpCfg->bBindMode) {
        s32Ret = CVI_SYS_Bind(&pstVpssGrpCfg->astChn[0], &pstVpssGrpCfg->astChn[1]);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_Bind failed with %#x\n", s32Ret);
            return s32Ret;
        }
    }

    return CVI_SUCCESS;
}

int app_ipcam_Vpss_DeInit(void)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    for (CVI_U32 VpssGrp = 0; VpssGrp < g_pstVpssCfg->u32GrpCnt; VpssGrp++) {
        s32Ret = app_ipcam_Vpss_Unbind(VpssGrp);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "Vpss grp(%d) Unbind failed with 0x%x!\n", VpssGrp, s32Ret);
            return s32Ret;
        }

        s32Ret = app_ipcam_Vpss_Destroy(VpssGrp);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "Vpss grp(%d) Destroy failed with 0x%x!\n", VpssGrp, s32Ret);
            return s32Ret;
        }
    }

    return CVI_SUCCESS;
}

int app_ipcam_Vpss_Init(void)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    APP_PROF_LOG_PRINT(LEVEL_INFO, "vpss init ------------------> start \n");

    for (CVI_U32 VpssGrp = 0; VpssGrp < g_pstVpssCfg->u32GrpCnt; VpssGrp++) {
        s32Ret = app_ipcam_Vpss_Create(VpssGrp);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "Vpss grp(%d) Create failed with 0x%x!\n", VpssGrp, s32Ret);
            return s32Ret;
        }
        s32Ret = app_ipcam_Vpss_Bind(VpssGrp);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "Vpss grp(%d) Bind failed with 0x%x!\n", VpssGrp, s32Ret);
            return s32Ret;
        }
    }

    APP_PROF_LOG_PRINT(LEVEL_INFO, "vpss init ------------------> end \n");

    return CVI_SUCCESS;
}


// venc.c




#define P_MAX_SIZE (2048 * 1024) //P oversize 512K lost it
/**************************************************************************
 *                              M A C R O S                               *
 **************************************************************************/
#define H26X_MAX_NUM_PACKS      8
#define JPEG_MAX_NUM_PACKS      1

/**************************************************************************
 *                           C O N S T A N T S                            *
 **************************************************************************/

/**************************************************************************
 *                          D A T A    T Y P E S                          *
 **************************************************************************/

/**************************************************************************
 *                         G L O B A L    D A T A                         *
 **************************************************************************/
static APP_PARAM_VENC_CTX_S g_stVencCtx = { 0 }, *g_pstVencCtx = &g_stVencCtx;

static pthread_t g_Venc_pthread[VENC_CHN_MAX] = { 0 };

static void *g_pDataCtx[VENC_CHN_MAX] = { NULL };
static void *g_pUserData[VENC_CHN_MAX][APP_DATA_COMSUMES_MAX] = { NULL };
static pfpDataConsumes g_Consumes[VENC_CHN_MAX][APP_DATA_COMSUMES_MAX] = { NULL };

/**************************************************************************
 *                 E X T E R N A L    R E F E R E N C E S                 *
 **************************************************************************/

/**************************************************************************
 *               F U N C T I O N    D E C L A R A T I O N S               *
 **************************************************************************/

APP_PARAM_VENC_CTX_S *app_ipcam_Venc_Param_Get(void)
{
    return g_pstVencCtx;
}

int app_ipcam_Venc_Consumes_Set(int chn, int index, pfpDataConsumes consume, void *pUserData)
{
    if (chn < 0 || chn >= VENC_CHN_MAX) {
        return -1;
    }
    if (index < 0 || index >= APP_DATA_COMSUMES_MAX) {
        return -1;
    }

    g_Consumes[chn][index] = consume;
    g_pUserData[chn][index] = pUserData;

    return 0;
}

/*
char* app_ipcam_Postfix_Get(PAYLOAD_TYPE_E enPayload)
{
    if (enPayload == PT_H264)
        return ".h264";
    else if (enPayload == PT_H265)
        return ".h265";
    else if (enPayload == PT_JPEG)
        return ".jpg";
    else if (enPayload == PT_MJPEG)
        return ".mjp";
    else {
        return NULL;
    }
}
*/

static CVI_S32 app_ipcam_Venc_FrameLost_Set(VENC_CHN VencChn, APP_FRAMELOST_PARAM_S *pFrameLostCfg)
{
    VENC_FRAMELOST_S stFL, *pstFL = &stFL;

    APP_CHK_RET(CVI_VENC_GetFrameLostStrategy(VencChn, pstFL), "get FrameLost Strategy");

    pstFL->bFrmLostOpen = (pFrameLostCfg->frameLost) == 1 ? CVI_TRUE : CVI_FALSE;
    pstFL->enFrmLostMode = FRMLOST_PSKIP;
    pstFL->u32EncFrmGaps = pFrameLostCfg->frameLostGap;
    pstFL->u32FrmLostBpsThr = pFrameLostCfg->frameLostBspThr;

    APP_CHK_RET(CVI_VENC_SetFrameLostStrategy(VencChn, pstFL), "set FrameLost Strategy");

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_H264Trans_Set(VENC_CHN VencChn)
{
    VENC_H264_TRANS_S h264Trans = { 0 };

    APP_H264_TRANS_PARAM_S stH264TransParam;
    memset(&stH264TransParam, 0, sizeof(APP_H264_TRANS_PARAM_S));
    APP_CHK_RET(CVI_VENC_GetH264Trans(VencChn, &h264Trans), "get H264 trans");
    stH264TransParam.h264ChromaQpOffset = 0;

    h264Trans.chroma_qp_index_offset = stH264TransParam.h264ChromaQpOffset;

    APP_CHK_RET(CVI_VENC_SetH264Trans(VencChn, &h264Trans), "set H264 trans");

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_H264Entropy_Set(VENC_CHN VencChn)
{
    APP_VENC_CHN_CFG_S *pstVencChnCfg = &g_pstVencCtx->astVencChnCfg[VencChn];
    VENC_H264_ENTROPY_S h264Entropy = { 0 };
    switch (pstVencChnCfg->u32Profile) {
        case H264E_PROFILE_BASELINE:
            h264Entropy.u32EntropyEncModeI = H264E_ENTROPY_CAVLC;
            h264Entropy.u32EntropyEncModeP = H264E_ENTROPY_CAVLC;
            break;
        default:
            h264Entropy.u32EntropyEncModeI = H264E_ENTROPY_CABAC;
            h264Entropy.u32EntropyEncModeP = H264E_ENTROPY_CABAC;
            break;
    }

    APP_CHK_RET(CVI_VENC_SetH264Entropy(VencChn, &h264Entropy), "set H264 entropy");

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_H264Vui_Set(VENC_CHN VencChn)
{
    VENC_H264_VUI_S h264Vui = { 0 };
    APP_VENC_CHN_CFG_S *pstVencChnCfg = &g_pstVencCtx->astVencChnCfg[VencChn];

    APP_H264_VUI_PARAM_S stH264VuiParam;
    memset(&stH264VuiParam, 0, sizeof(APP_H264_VUI_PARAM_S));
    APP_CHK_RET(CVI_VENC_GetH264Vui(VencChn, &h264Vui), "get H264 Vui");
    stH264VuiParam.aspectRatioInfoPresentFlag   = CVI_H26X_ASPECT_RATIO_INFO_PRESENT_FLAG_DEFAULT;
    stH264VuiParam.aspectRatioIdc               = CVI_H26X_ASPECT_RATIO_IDC_DEFAULT;
    stH264VuiParam.overscanInfoPresentFlag      = CVI_H26X_OVERSCAN_INFO_PRESENT_FLAG_DEFAULT;
    stH264VuiParam.overscanAppropriateFlag      = CVI_H26X_OVERSCAN_APPROPRIATE_FLAG_DEFAULT;
    stH264VuiParam.sarWidth                     = CVI_H26X_SAR_WIDTH_DEFAULT;
    stH264VuiParam.sarHeight                    = CVI_H26X_SAR_HEIGHT_DEFAULT;
    stH264VuiParam.fixedFrameRateFlag           = CVI_H264_FIXED_FRAME_RATE_FLAG_DEFAULT;
    stH264VuiParam.videoSignalTypePresentFlag   = CVI_H26X_VIDEO_SIGNAL_TYPE_PRESENT_FLAG_DEFAULT;
    stH264VuiParam.videoFormat                  = CVI_H26X_VIDEO_FORMAT_DEFAULT;
    stH264VuiParam.videoFullRangeFlag           = CVI_H26X_VIDEO_FULL_RANGE_FLAG_DEFAULT;
    stH264VuiParam.colourDescriptionPresentFlag = CVI_H26X_COLOUR_DESCRIPTION_PRESENT_FLAG_DEFAULT;
    stH264VuiParam.colourPrimaries              = CVI_H26X_COLOUR_PRIMARIES_DEFAULT;
    stH264VuiParam.transferCharacteristics      = CVI_H26X_TRANSFER_CHARACTERISTICS_DEFAULT;
    stH264VuiParam.matrixCoefficients           = CVI_H26X_MATRIX_COEFFICIENTS_DEFAULT;
    
    stH264VuiParam.timingInfoPresentFlag = 1;
    stH264VuiParam.numUnitsInTick = 1;

    h264Vui.stVuiAspectRatio.aspect_ratio_info_present_flag = stH264VuiParam.aspectRatioInfoPresentFlag;
    if (h264Vui.stVuiAspectRatio.aspect_ratio_info_present_flag) {
        h264Vui.stVuiAspectRatio.aspect_ratio_idc = stH264VuiParam.aspectRatioIdc;
        h264Vui.stVuiAspectRatio.sar_width = stH264VuiParam.sarWidth;
        h264Vui.stVuiAspectRatio.sar_height = stH264VuiParam.sarHeight;
    }

    h264Vui.stVuiAspectRatio.overscan_info_present_flag = stH264VuiParam.overscanInfoPresentFlag;
    if (h264Vui.stVuiAspectRatio.overscan_info_present_flag) {
        h264Vui.stVuiAspectRatio.overscan_appropriate_flag = stH264VuiParam.overscanAppropriateFlag;
    }

    h264Vui.stVuiTimeInfo.timing_info_present_flag = stH264VuiParam.timingInfoPresentFlag;
    if (h264Vui.stVuiTimeInfo.timing_info_present_flag) {
        h264Vui.stVuiTimeInfo.fixed_frame_rate_flag = stH264VuiParam.fixedFrameRateFlag;
        h264Vui.stVuiTimeInfo.num_units_in_tick = stH264VuiParam.numUnitsInTick;
        //264 fps = time_scale / (2 * num_units_in_tick) 
        h264Vui.stVuiTimeInfo.time_scale = pstVencChnCfg->u32DstFrameRate * 2 * stH264VuiParam.numUnitsInTick;
    }

    h264Vui.stVuiVideoSignal.video_signal_type_present_flag = stH264VuiParam.videoSignalTypePresentFlag;
    if (h264Vui.stVuiVideoSignal.video_signal_type_present_flag) {
        h264Vui.stVuiVideoSignal.video_format = stH264VuiParam.videoFormat;
        h264Vui.stVuiVideoSignal.video_full_range_flag = stH264VuiParam.videoFullRangeFlag;
        h264Vui.stVuiVideoSignal.colour_description_present_flag = stH264VuiParam.colourDescriptionPresentFlag;
        if (h264Vui.stVuiVideoSignal.colour_description_present_flag) {
            h264Vui.stVuiVideoSignal.colour_primaries = stH264VuiParam.colourPrimaries;
            h264Vui.stVuiVideoSignal.transfer_characteristics = stH264VuiParam.transferCharacteristics;
            h264Vui.stVuiVideoSignal.matrix_coefficients = stH264VuiParam.matrixCoefficients;
        }
    }

    APP_CHK_RET(CVI_VENC_SetH264Vui(VencChn, &h264Vui), "set H264 Vui");

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_H265Trans_Set(VENC_CHN VencChn)
{
    VENC_H265_TRANS_S h265Trans = { 0 };

    APP_H265_TRANS_PARAM_S stH265TransParam;
    memset(&stH265TransParam, 0, sizeof(APP_H265_TRANS_PARAM_S));
    APP_CHK_RET(CVI_VENC_GetH265Trans(VencChn, &h265Trans), "get H265 Trans");
    stH265TransParam.h265CbQpOffset = 0;
    stH265TransParam.h265CrQpOffset = 0;

    h265Trans.cb_qp_offset = stH265TransParam.h265CbQpOffset;
    h265Trans.cr_qp_offset = stH265TransParam.h265CrQpOffset;

    APP_CHK_RET(CVI_VENC_SetH265Trans(VencChn, &h265Trans), "set H265 Trans");

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_H265Vui_Set(VENC_CHN VencChn)
{
    VENC_H265_VUI_S h265Vui = { 0 };
    APP_VENC_CHN_CFG_S *pstVencChnCfg = &g_pstVencCtx->astVencChnCfg[VencChn];

    APP_H265_VUI_PARAM_S stH265VuiParam;
    memset(&stH265VuiParam, 0, sizeof(APP_H265_VUI_PARAM_S));
    APP_CHK_RET(CVI_VENC_GetH265Vui(VencChn, &h265Vui), "get H265 Vui");
    stH265VuiParam.aspectRatioInfoPresentFlag   = CVI_H26X_ASPECT_RATIO_INFO_PRESENT_FLAG_DEFAULT;
    stH265VuiParam.aspectRatioIdc               = CVI_H26X_ASPECT_RATIO_IDC_DEFAULT;
    stH265VuiParam.overscanInfoPresentFlag      = CVI_H26X_OVERSCAN_INFO_PRESENT_FLAG_DEFAULT;
    stH265VuiParam.overscanAppropriateFlag      = CVI_H26X_OVERSCAN_APPROPRIATE_FLAG_DEFAULT;
    stH265VuiParam.sarWidth                     = CVI_H26X_SAR_WIDTH_DEFAULT;
    stH265VuiParam.sarHeight                    = CVI_H26X_SAR_HEIGHT_DEFAULT;
    stH265VuiParam.videoSignalTypePresentFlag   = CVI_H26X_VIDEO_SIGNAL_TYPE_PRESENT_FLAG_DEFAULT;
    stH265VuiParam.videoFormat                  = CVI_H26X_VIDEO_FORMAT_DEFAULT;
    stH265VuiParam.videoFullRangeFlag           = CVI_H26X_VIDEO_FULL_RANGE_FLAG_DEFAULT;
    stH265VuiParam.colourDescriptionPresentFlag = CVI_H26X_COLOUR_DESCRIPTION_PRESENT_FLAG_DEFAULT;
    stH265VuiParam.colourPrimaries              = CVI_H26X_COLOUR_PRIMARIES_DEFAULT;
    stH265VuiParam.transferCharacteristics      = CVI_H26X_TRANSFER_CHARACTERISTICS_DEFAULT;
    stH265VuiParam.matrixCoefficients           = CVI_H26X_MATRIX_COEFFICIENTS_DEFAULT;

    stH265VuiParam.timingInfoPresentFlag = 1;
    stH265VuiParam.numUnitsInTick = 1;

    h265Vui.stVuiAspectRatio.aspect_ratio_info_present_flag = stH265VuiParam.aspectRatioInfoPresentFlag;
    if (h265Vui.stVuiAspectRatio.aspect_ratio_info_present_flag) {
        h265Vui.stVuiAspectRatio.aspect_ratio_idc = stH265VuiParam.aspectRatioIdc;
        h265Vui.stVuiAspectRatio.sar_width = stH265VuiParam.sarWidth;
        h265Vui.stVuiAspectRatio.sar_height = stH265VuiParam.sarHeight;
    }

    h265Vui.stVuiAspectRatio.overscan_info_present_flag = stH265VuiParam.overscanInfoPresentFlag;
    if (h265Vui.stVuiAspectRatio.overscan_info_present_flag) {
        h265Vui.stVuiAspectRatio.overscan_appropriate_flag = stH265VuiParam.overscanAppropriateFlag;
    }

    h265Vui.stVuiTimeInfo.timing_info_present_flag = stH265VuiParam.timingInfoPresentFlag;
    if (h265Vui.stVuiTimeInfo.timing_info_present_flag) {
        h265Vui.stVuiTimeInfo.num_units_in_tick = stH265VuiParam.numUnitsInTick;
        //265 fps = time_scale / num_units_in_tick
        h265Vui.stVuiTimeInfo.time_scale = pstVencChnCfg->u32DstFrameRate * stH265VuiParam.numUnitsInTick;
    }

    h265Vui.stVuiVideoSignal.video_signal_type_present_flag = stH265VuiParam.videoSignalTypePresentFlag;
    if (h265Vui.stVuiVideoSignal.video_signal_type_present_flag) {
        h265Vui.stVuiVideoSignal.video_format = stH265VuiParam.videoFormat;
        h265Vui.stVuiVideoSignal.video_full_range_flag = stH265VuiParam.videoFullRangeFlag;
        h265Vui.stVuiVideoSignal.colour_description_present_flag = stH265VuiParam.colourDescriptionPresentFlag;
        if (h265Vui.stVuiVideoSignal.colour_description_present_flag) {
            h265Vui.stVuiVideoSignal.colour_primaries = stH265VuiParam.colourPrimaries;
            h265Vui.stVuiVideoSignal.transfer_characteristics = stH265VuiParam.transferCharacteristics;
            h265Vui.stVuiVideoSignal.matrix_coefficients = stH265VuiParam.matrixCoefficients;
        }
    }

    APP_CHK_RET(CVI_VENC_SetH265Vui(VencChn, &h265Vui), "set H265 Vui");

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_Roi_Set(VENC_CHN vencChn, APP_VENC_ROI_CFG_S *pstRoiAttr)
{
    if (NULL == pstRoiAttr)
    {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "pstRoiAttr is NULL!\n");
        return CVI_FAILURE;
    }

    CVI_S32 ret;
    CVI_S32 i = 0;
    VENC_ROI_ATTR_S roiAttr;
    memset(&roiAttr, 0, sizeof(roiAttr));

    for (i = 0; i < MAX_NUM_ROI; i++)
    {
        if (vencChn == pstRoiAttr[i].VencChn)
        {
            ret = CVI_VENC_GetRoiAttr(vencChn, i, &roiAttr);
            if (ret != CVI_SUCCESS)
            {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "GetRoiAttr failed!\n");
                return CVI_FAILURE;
            }
            roiAttr.bEnable = pstRoiAttr[i].bEnable;
            roiAttr.bAbsQp = pstRoiAttr[i].bAbsQp;
            roiAttr.u32Index = i;
            roiAttr.s32Qp = pstRoiAttr[i].u32Qp;
            roiAttr.stRect.s32X = pstRoiAttr[i].u32X;
            roiAttr.stRect.s32Y = pstRoiAttr[i].u32Y;
            roiAttr.stRect.u32Width = pstRoiAttr[i].u32Width;
            roiAttr.stRect.u32Height = pstRoiAttr[i].u32Height;
            ret = CVI_VENC_SetRoiAttr(vencChn, &roiAttr);
            if (ret != CVI_SUCCESS)
            {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "SetRoiAttr failed!\n");
                return CVI_FAILURE;
            }
        }
    }

    return CVI_SUCCESS;
}

static CVI_S32 app_ipcam_Venc_Jpeg_Param_Set(VENC_CHN VencChn, APP_JPEG_CODEC_PARAM_S *pstJpegCodecCfg)
{
    VENC_JPEG_PARAM_S stJpegParam, *pstJpegParam = &stJpegParam;

    APP_CHK_RET(CVI_VENC_GetJpegParam(VencChn, pstJpegParam), "get jpeg param");

    if (pstJpegCodecCfg->quality <= 0)
        pstJpegCodecCfg->quality = 1;
    else if (pstJpegCodecCfg->quality >= 100)
        pstJpegCodecCfg->quality = 99;

    pstJpegParam->u32Qfactor = pstJpegCodecCfg->quality;
    pstJpegParam->u32MCUPerECS = pstJpegCodecCfg->MCUPerECS;

    APP_CHK_RET(CVI_VENC_SetJpegParam(VencChn, pstJpegParam), "set jpeg param");

    return CVI_SUCCESS;
}

static int app_ipcam_Venc_Chn_Attr_Set(VENC_ATTR_S *pstVencAttr, APP_VENC_CHN_CFG_S *pstVencChnCfg)
{
    pstVencAttr->enType = pstVencChnCfg->enType;
    pstVencAttr->u32MaxPicWidth = pstVencChnCfg->u32Width;
    pstVencAttr->u32MaxPicHeight = pstVencChnCfg->u32Height;
    pstVencAttr->u32PicWidth = pstVencChnCfg->u32Width;
    pstVencAttr->u32PicHeight = pstVencChnCfg->u32Height;
    pstVencAttr->u32Profile = pstVencChnCfg->u32Profile;
    pstVencAttr->bSingleCore = pstVencChnCfg->bSingleCore;
    pstVencAttr->bByFrame = CVI_TRUE;
    pstVencAttr->bEsBufQueueEn = CVI_TRUE;
    pstVencAttr->bIsoSendFrmEn = true;
    pstVencAttr->u32BufSize = pstVencChnCfg->u32StreamBufSize;

    APP_PROF_LOG_PRINT(LEVEL_TRACE,"enType=%d u32Profile=%d bSingleCore=%d\n",
        pstVencAttr->enType, pstVencAttr->u32Profile, pstVencAttr->bSingleCore);
    APP_PROF_LOG_PRINT(LEVEL_TRACE,"u32MaxPicWidth=%d u32MaxPicHeight=%d u32PicWidth=%d u32PicHeight=%d\n",
        pstVencAttr->u32MaxPicWidth, pstVencAttr->u32MaxPicHeight, pstVencAttr->u32PicWidth, pstVencAttr->u32PicHeight);

    /* Venc encode type validity check */
    if ((pstVencAttr->enType != PT_H265) && (pstVencAttr->enType != PT_H264) 
        && (pstVencAttr->enType != PT_JPEG) && (pstVencAttr->enType != PT_MJPEG)) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR,"encode type = %d invalid\n", pstVencAttr->enType);
        return CVI_FAILURE;
    }

    if (pstVencAttr->enType == PT_H264) {
        // pstVencAttr->stAttrH264e.bSingleLumaBuf = 1;
    }

    if (PT_JPEG == pstVencChnCfg->enType || PT_MJPEG == pstVencChnCfg->enType) {
        VENC_ATTR_JPEG_S *pstJpegAttr = &pstVencAttr->stAttrJpege;

        pstJpegAttr->bSupportDCF = CVI_FALSE;
        pstJpegAttr->stMPFCfg.u8LargeThumbNailNum = 0;
        pstJpegAttr->enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Venc_Gop_Attr_Set(VENC_GOP_ATTR_S *pstGopAttr, APP_VENC_CHN_CFG_S *pstVencChnCfg)
{
    VENC_GOP_MODE_E enGopMode = pstVencChnCfg->enGopMode;

    /* Venc gop mode validity check */
    if ((enGopMode != VENC_GOPMODE_NORMALP) && (enGopMode != VENC_GOPMODE_SMARTP) 
        && (enGopMode != VENC_GOPMODE_DUALP) && (enGopMode != VENC_GOPMODE_BIPREDB)) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR,"gop mode = %d invalid\n", enGopMode);
        return CVI_FAILURE;
    }

    switch (enGopMode) {
        case VENC_GOPMODE_NORMALP:
            pstGopAttr->stNormalP.s32IPQpDelta = pstVencChnCfg->unGopParam.stNormalP.s32IPQpDelta;

            APP_PROF_LOG_PRINT(LEVEL_TRACE,"stNormalP -> s32IPQpDelta=%d\n",
                pstGopAttr->stNormalP.s32IPQpDelta);
            break;
        case VENC_GOPMODE_SMARTP:
            pstGopAttr->stSmartP.s32BgQpDelta = pstVencChnCfg->unGopParam.stSmartP.s32BgQpDelta;
            pstGopAttr->stSmartP.s32ViQpDelta = pstVencChnCfg->unGopParam.stSmartP.s32ViQpDelta;
            pstGopAttr->stSmartP.u32BgInterval = pstVencChnCfg->unGopParam.stSmartP.u32BgInterval;

            APP_PROF_LOG_PRINT(LEVEL_TRACE,"stSmartP -> s32BgQpDelta=%d s32ViQpDelta=%d u32BgInterval=%d\n",
                pstGopAttr->stSmartP.s32BgQpDelta, pstGopAttr->stSmartP.s32ViQpDelta, pstGopAttr->stSmartP.u32BgInterval);
            break;

        case VENC_GOPMODE_DUALP:
            pstGopAttr->stDualP.s32IPQpDelta = pstVencChnCfg->unGopParam.stDualP.s32IPQpDelta;
            pstGopAttr->stDualP.s32SPQpDelta = pstVencChnCfg->unGopParam.stDualP.s32SPQpDelta;
            pstGopAttr->stDualP.u32SPInterval = pstVencChnCfg->unGopParam.stDualP.u32SPInterval;

            APP_PROF_LOG_PRINT(LEVEL_TRACE,"stDualP -> s32IPQpDelta=%d s32SPQpDelta=%d u32SPInterval=%d\n",
                pstGopAttr->stDualP.s32IPQpDelta, pstGopAttr->stDualP.s32SPQpDelta, pstGopAttr->stDualP.u32SPInterval);
            break;

        case VENC_GOPMODE_BIPREDB:
            pstGopAttr->stBipredB.s32BQpDelta = pstVencChnCfg->unGopParam.stBipredB.s32BQpDelta;
            pstGopAttr->stBipredB.s32IPQpDelta = pstVencChnCfg->unGopParam.stBipredB.s32IPQpDelta;
            pstGopAttr->stBipredB.u32BFrmNum = pstVencChnCfg->unGopParam.stBipredB.u32BFrmNum;

            APP_PROF_LOG_PRINT(LEVEL_TRACE,"stBipredB -> s32BQpDelta=%d s32IPQpDelta=%d u32BFrmNum=%d\n",
                pstGopAttr->stBipredB.s32BQpDelta, pstGopAttr->stBipredB.s32IPQpDelta, pstGopAttr->stBipredB.u32BFrmNum);
            break;

        default:
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"not support the gop mode !\n");
            return CVI_FAILURE;
    }

    pstGopAttr->enGopMode = enGopMode;
    if (PT_MJPEG == pstVencChnCfg->enType || PT_JPEG == pstVencChnCfg->enType) {
        pstGopAttr->enGopMode = VENC_GOPMODE_NORMALP;
        pstGopAttr->stNormalP.s32IPQpDelta = pstVencChnCfg->unGopParam.stNormalP.s32IPQpDelta;
    }

    return CVI_SUCCESS;
}

static int app_ipcam_Venc_Rc_Attr_Set(VENC_RC_ATTR_S *pstRCAttr, APP_VENC_CHN_CFG_S *pstVencChnCfg)
{
    int SrcFrmRate = pstVencChnCfg->u32SrcFrameRate;
    int DstFrmRate = pstVencChnCfg->u32DstFrameRate;
    int BitRate    = pstVencChnCfg->u32BitRate;
    int MaxBitrate = pstVencChnCfg->u32MaxBitRate;
    int StatTime   = pstVencChnCfg->statTime;
    int Gop        = pstVencChnCfg->u32Gop;
    int IQP        = pstVencChnCfg->u32IQp;
    int PQP        = pstVencChnCfg->u32PQp;

    APP_PROF_LOG_PRINT(LEVEL_TRACE,"RcMode=%d EncType=%d SrcFR=%d DstFR=%d\n", 
        pstVencChnCfg->enRcMode, pstVencChnCfg->enType, 
        pstVencChnCfg->u32SrcFrameRate, pstVencChnCfg->u32DstFrameRate);
    APP_PROF_LOG_PRINT(LEVEL_TRACE,"BR=%d MaxBR=%d statTime=%d gop=%d IQP=%d PQP=%d\n", 
        pstVencChnCfg->u32BitRate, pstVencChnCfg->u32MaxBitRate, 
        pstVencChnCfg->statTime, pstVencChnCfg->u32Gop, pstVencChnCfg->u32IQp, pstVencChnCfg->u32PQp);

    pstRCAttr->enRcMode = pstVencChnCfg->enRcMode;
    switch (pstVencChnCfg->enType) {
    case PT_H265: {
        if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H265CBR) {
            VENC_H265_CBR_S *pstH265Cbr = &pstRCAttr->stH265Cbr;

            pstH265Cbr->u32Gop = Gop;
            pstH265Cbr->u32StatTime = StatTime;
            pstH265Cbr->u32SrcFrameRate = SrcFrmRate;
            pstH265Cbr->fr32DstFrameRate = DstFrmRate;
            pstH265Cbr->u32BitRate = BitRate;
            pstH265Cbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H265FIXQP) {
            VENC_H265_FIXQP_S *pstH265FixQp = &pstRCAttr->stH265FixQp;

            pstH265FixQp->u32Gop = Gop;
            pstH265FixQp->u32SrcFrameRate = SrcFrmRate;
            pstH265FixQp->fr32DstFrameRate = DstFrmRate;
            pstH265FixQp->u32IQp = IQP;
            pstH265FixQp->u32PQp = PQP;
            pstH265FixQp->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H265VBR) {
            VENC_H265_VBR_S *pstH265Vbr = &pstRCAttr->stH265Vbr;

            pstH265Vbr->u32Gop = Gop;
            pstH265Vbr->u32StatTime = StatTime;
            pstH265Vbr->u32SrcFrameRate = SrcFrmRate;
            pstH265Vbr->fr32DstFrameRate = DstFrmRate;
            pstH265Vbr->u32MaxBitRate = MaxBitrate;
            pstH265Vbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H265AVBR) {
            VENC_H265_AVBR_S *pstH265AVbr = &pstRCAttr->stH265AVbr;

            pstH265AVbr->u32Gop = Gop;
            pstH265AVbr->u32StatTime = StatTime;
            pstH265AVbr->u32SrcFrameRate = SrcFrmRate;
            pstH265AVbr->fr32DstFrameRate = DstFrmRate;
            pstH265AVbr->u32MaxBitRate = MaxBitrate;
            pstH265AVbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H265QVBR) {
            VENC_H265_QVBR_S *pstH265QVbr = &pstRCAttr->stH265QVbr;

            pstH265QVbr->u32Gop = Gop;
            pstH265QVbr->u32StatTime = StatTime;
            pstH265QVbr->u32SrcFrameRate = SrcFrmRate;
            pstH265QVbr->fr32DstFrameRate = DstFrmRate;
            pstH265QVbr->u32TargetBitRate = BitRate;
            pstH265QVbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H265QPMAP) {
            VENC_H265_QPMAP_S *pstH265QpMap = &pstRCAttr->stH265QpMap;

            pstH265QpMap->u32Gop = Gop;
            pstH265QpMap->u32StatTime = StatTime;
            pstH265QpMap->u32SrcFrameRate = SrcFrmRate;
            pstH265QpMap->fr32DstFrameRate = DstFrmRate;
            pstH265QpMap->enQpMapMode = VENC_RC_QPMAP_MODE_MEANQP;
            pstH265QpMap->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"VencChn(%d) enRcMode(%d) not support\n", pstVencChnCfg->VencChn, pstVencChnCfg->enRcMode);
            return CVI_FAILURE;
        }
    }
    break;
    case PT_H264: {
        if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H264CBR) {
            VENC_H264_CBR_S *pstH264Cbr = &pstRCAttr->stH264Cbr;

            pstH264Cbr->u32Gop = Gop;
            pstH264Cbr->u32StatTime = StatTime;
            pstH264Cbr->u32SrcFrameRate = SrcFrmRate;
            pstH264Cbr->fr32DstFrameRate = DstFrmRate;
            pstH264Cbr->u32BitRate = BitRate;
            pstH264Cbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H264FIXQP) {
            VENC_H264_FIXQP_S *pstH264FixQp = &pstRCAttr->stH264FixQp;

            pstH264FixQp->u32Gop = Gop;
            pstH264FixQp->u32SrcFrameRate = SrcFrmRate;
            pstH264FixQp->fr32DstFrameRate = DstFrmRate;
            pstH264FixQp->u32IQp = IQP;
            pstH264FixQp->u32PQp = PQP;
            pstH264FixQp->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H264VBR) {
            VENC_H264_VBR_S *pstH264Vbr = &pstRCAttr->stH264Vbr;

            pstH264Vbr->u32Gop = Gop;
            pstH264Vbr->u32StatTime = StatTime;
            pstH264Vbr->u32SrcFrameRate = SrcFrmRate;
            pstH264Vbr->fr32DstFrameRate = DstFrmRate;
            pstH264Vbr->u32MaxBitRate = MaxBitrate;
            pstH264Vbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H264AVBR) {
            VENC_H264_AVBR_S *pstH264AVbr = &pstRCAttr->stH264AVbr;

            pstH264AVbr->u32Gop = Gop;
            pstH264AVbr->u32StatTime = StatTime;
            pstH264AVbr->u32SrcFrameRate = SrcFrmRate;
            pstH264AVbr->fr32DstFrameRate = DstFrmRate;
            pstH264AVbr->u32MaxBitRate = MaxBitrate;
            pstH264AVbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H264QVBR) {
            VENC_H264_QVBR_S *pstH264QVbr = &pstRCAttr->stH264QVbr;

            pstH264QVbr->u32Gop = Gop;
            pstH264QVbr->u32StatTime = StatTime;
            pstH264QVbr->u32SrcFrameRate = SrcFrmRate;
            pstH264QVbr->fr32DstFrameRate = DstFrmRate;
            pstH264QVbr->u32TargetBitRate = BitRate;
            pstH264QVbr->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_H264QPMAP) {
            VENC_H264_QPMAP_S *pstH264QpMap = &pstRCAttr->stH264QpMap;

            pstH264QpMap->u32Gop = Gop;
            pstH264QpMap->u32StatTime = StatTime;
            pstH264QpMap->u32SrcFrameRate = SrcFrmRate;
            pstH264QpMap->fr32DstFrameRate = DstFrmRate;
            pstH264QpMap->bVariFpsEn = (pstVencChnCfg->VencChn == 0) ? 0 : 1; //SBM unsupport
        } else {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"enRcMode(%d) not support\n", pstVencChnCfg->enRcMode);
            return CVI_FAILURE;
        }
    }
    break;
    case PT_MJPEG: {
        if (pstVencChnCfg->enRcMode == VENC_RC_MODE_MJPEGFIXQP) {
            VENC_MJPEG_FIXQP_S *pstMjpegeFixQp = &pstRCAttr->stMjpegFixQp;

            // 0 use old q-table for forward compatible.
            pstMjpegeFixQp->u32Qfactor = 0;
            pstMjpegeFixQp->u32SrcFrameRate = SrcFrmRate;
            pstMjpegeFixQp->fr32DstFrameRate = DstFrmRate;
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_MJPEGCBR) {
            VENC_MJPEG_CBR_S *pstMjpegeCbr = &pstRCAttr->stMjpegCbr;

            pstMjpegeCbr->u32StatTime = StatTime;
            pstMjpegeCbr->u32SrcFrameRate = SrcFrmRate;
            pstMjpegeCbr->fr32DstFrameRate = DstFrmRate;
            pstMjpegeCbr->u32BitRate = BitRate;
        } else if (pstVencChnCfg->enRcMode == VENC_RC_MODE_MJPEGVBR) {
            VENC_MJPEG_VBR_S *pstMjpegeVbr = &pstRCAttr->stMjpegVbr;

            pstMjpegeVbr->u32StatTime = StatTime;
            pstMjpegeVbr->u32SrcFrameRate = SrcFrmRate;
            pstMjpegeVbr->fr32DstFrameRate = DstFrmRate;
            pstMjpegeVbr->u32MaxBitRate = MaxBitrate;
        } else {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"cann't support other mode(%d) in this version!\n", pstVencChnCfg->enRcMode);
            return CVI_FAILURE;
        }
    }
    break;

    case PT_JPEG:
        break;

    default:
        APP_PROF_LOG_PRINT(LEVEL_ERROR,"cann't support this enType (%d) in this version!\n", pstVencChnCfg->enType);
        return CVI_ERR_VENC_NOT_SUPPORT;
    }

    return CVI_SUCCESS;
}

static void app_ipcam_Venc_Attr_Check(VENC_CHN_ATTR_S *pstVencChnAttr)
{
    if (pstVencChnAttr->stVencAttr.enType == PT_H264) {
        // pstVencChnAttr->stVencAttr.stAttrH264e.bSingleLumaBuf = 1;
    }

    if ((pstVencChnAttr->stGopAttr.enGopMode == VENC_GOPMODE_BIPREDB) &&
        (pstVencChnAttr->stVencAttr.enType == PT_H264)) {
        if (pstVencChnAttr->stVencAttr.u32Profile == 0) {
            pstVencChnAttr->stVencAttr.u32Profile = 1;
            APP_PROF_LOG_PRINT(LEVEL_WARN,"H.264 base not support BIPREDB, change to main\n");
        }
    }

    if ((pstVencChnAttr->stRcAttr.enRcMode == VENC_RC_MODE_H264QPMAP) ||
        (pstVencChnAttr->stRcAttr.enRcMode == VENC_RC_MODE_H265QPMAP)) {
        if (pstVencChnAttr->stGopAttr.enGopMode == VENC_GOPMODE_ADVSMARTP) {
            pstVencChnAttr->stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
            APP_PROF_LOG_PRINT(LEVEL_WARN,"advsmartp not support QPMAP, so change gopmode to smartp!\n");
        }
    }

    if ((pstVencChnAttr->stGopAttr.enGopMode == VENC_GOPMODE_BIPREDB) &&
        (pstVencChnAttr->stVencAttr.enType == PT_H264)) {
        if (pstVencChnAttr->stVencAttr.u32Profile == 0) {
            pstVencChnAttr->stVencAttr.u32Profile = 1;
            APP_PROF_LOG_PRINT(LEVEL_WARN,"H.264 base not support BIPREDB, change to main\n");
        }
    }
    if ((pstVencChnAttr->stRcAttr.enRcMode == VENC_RC_MODE_H264QPMAP) ||
        (pstVencChnAttr->stRcAttr.enRcMode == VENC_RC_MODE_H265QPMAP)) {
        if (pstVencChnAttr->stGopAttr.enGopMode == VENC_GOPMODE_ADVSMARTP) {
            pstVencChnAttr->stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
            APP_PROF_LOG_PRINT(LEVEL_WARN,"advsmartp not support QPMAP, so change gopmode to smartp!\n");
        }
    }
}

static int app_ipcam_Venc_Rc_Param_Set(
    VENC_CHN VencChn,
    PAYLOAD_TYPE_E enCodecType,
    VENC_RC_MODE_E enRcMode,
    APP_RC_PARAM_S *pstRcParamCfg)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VENC_RC_PARAM_S stRcParam, *pstRcParam = &stRcParam;

    s32Ret = CVI_VENC_GetRcParam(VencChn, pstRcParam);
    if (s32Ret != CVI_SUCCESS) {
        printf("[ERR] GetRcParam, 0x%X\n", s32Ret);
        return s32Ret;
    }

    CVI_U32 u32MaxIprop          = pstRcParamCfg->u32MaxIprop;
    CVI_U32 u32MinIprop          = pstRcParamCfg->u32MinIprop;
    CVI_U32 u32MaxQp             = pstRcParamCfg->u32MaxQp;
    CVI_U32 u32MinQp             = pstRcParamCfg->u32MinQp;
    CVI_U32 u32MaxIQp            = pstRcParamCfg->u32MaxIQp;
    CVI_U32 u32MinIQp            = pstRcParamCfg->u32MinIQp;
    CVI_S32 s32ChangePos         = pstRcParamCfg->s32ChangePos;
    CVI_S32 s32MinStillPercent   = pstRcParamCfg->s32MinStillPercent;
    CVI_U32 u32MaxStillQP        = pstRcParamCfg->u32MaxStillQP;
    CVI_U32 u32MotionSensitivity = pstRcParamCfg->u32MotionSensitivity;
    CVI_S32 s32AvbrFrmLostOpen   = pstRcParamCfg->s32AvbrFrmLostOpen;
    CVI_S32 s32AvbrFrmGap        = pstRcParamCfg->s32AvbrFrmGap;
    CVI_S32 s32AvbrPureStillThr  = pstRcParamCfg->s32AvbrPureStillThr;

    CVI_U32 u32ThrdLv            = pstRcParamCfg->u32ThrdLv;
    CVI_S32 s32FirstFrameStartQp = pstRcParamCfg->s32FirstFrameStartQp;
    CVI_S32 s32InitialDelay      = pstRcParamCfg->s32InitialDelay;

    APP_PROF_LOG_PRINT(LEVEL_TRACE,"MaxIprop=%d MinIprop=%d MaxQp=%d MinQp=%d MaxIQp=%d MinIQp=%d\n",
        u32MaxIprop, u32MinIprop, u32MaxQp, u32MinQp, u32MaxIQp, u32MinIQp);
    APP_PROF_LOG_PRINT(LEVEL_TRACE,"ChangePos=%d MinStillPercent=%d MaxStillQP=%d MotionSensitivity=%d AvbrFrmLostOpen=%d\n",
        s32ChangePos, s32MinStillPercent, u32MaxStillQP, u32MotionSensitivity, s32AvbrFrmLostOpen);
    APP_PROF_LOG_PRINT(LEVEL_TRACE,"AvbrFrmGap=%d AvbrPureStillThr=%d ThrdLv=%d FirstFrameStartQp=%d InitialDelay=%d\n",
        s32AvbrFrmGap, s32AvbrPureStillThr, u32ThrdLv, s32FirstFrameStartQp, s32InitialDelay);

    pstRcParam->u32ThrdLv = u32ThrdLv;
    pstRcParam->s32FirstFrameStartQp = s32FirstFrameStartQp;
    pstRcParam->s32InitialDelay = s32InitialDelay;

    switch (enCodecType) {
    case PT_H265: {
        if (enRcMode == VENC_RC_MODE_H265CBR) {
            pstRcParam->stParamH265Cbr.u32MaxIprop = u32MaxIprop;
            pstRcParam->stParamH265Cbr.u32MinIprop = u32MinIprop;
            pstRcParam->stParamH265Cbr.u32MaxIQp = u32MaxIQp;
            pstRcParam->stParamH265Cbr.u32MinIQp = u32MinIQp;
            pstRcParam->stParamH265Cbr.u32MaxQp = u32MaxQp;
            pstRcParam->stParamH265Cbr.u32MinQp = u32MinQp;
        } else if (enRcMode == VENC_RC_MODE_H265VBR) {
            pstRcParam->stParamH265Vbr.u32MaxIprop = u32MaxIprop;
            pstRcParam->stParamH265Vbr.u32MinIprop = u32MinIprop;
            pstRcParam->stParamH265Vbr.u32MaxIQp = u32MaxIQp;
            pstRcParam->stParamH265Vbr.u32MinIQp = u32MinIQp;
            pstRcParam->stParamH265Vbr.u32MaxQp = u32MaxQp;
            pstRcParam->stParamH265Vbr.u32MinQp = u32MinQp;
            pstRcParam->stParamH265Vbr.s32ChangePos = s32ChangePos;
        } else if (enRcMode == VENC_RC_MODE_H265AVBR) {
            pstRcParam->stParamH265AVbr.u32MaxIprop = u32MaxIprop;
            pstRcParam->stParamH265AVbr.u32MinIprop = u32MinIprop;
            pstRcParam->stParamH265AVbr.u32MaxIQp = u32MaxIQp;
            pstRcParam->stParamH265AVbr.u32MinIQp = u32MinIQp;
            pstRcParam->stParamH265AVbr.u32MaxQp = u32MaxQp;
            pstRcParam->stParamH265AVbr.u32MinQp = u32MinQp;
            pstRcParam->stParamH265AVbr.s32ChangePos = s32ChangePos;
            pstRcParam->stParamH265AVbr.s32MinStillPercent = s32MinStillPercent;
            pstRcParam->stParamH265AVbr.u32MaxStillQP = u32MaxStillQP;
            pstRcParam->stParamH265AVbr.u32MotionSensitivity = u32MotionSensitivity;
            pstRcParam->stParamH265AVbr.s32AvbrFrmLostOpen = s32AvbrFrmLostOpen;
            pstRcParam->stParamH265AVbr.s32AvbrFrmGap = s32AvbrFrmGap;
            pstRcParam->stParamH265AVbr.s32AvbrPureStillThr = s32AvbrPureStillThr;
        } else {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"enRcMode(%d) not support\n", enRcMode);
            return CVI_FAILURE;
        }
    }
    break;
    case PT_H264: {
        if (enRcMode == VENC_RC_MODE_H264CBR) {
            pstRcParam->stParamH264Cbr.u32MaxIprop = u32MaxIprop;
            pstRcParam->stParamH264Cbr.u32MinIprop = u32MinIprop;
            pstRcParam->stParamH264Cbr.u32MaxIQp = u32MaxIQp;
            pstRcParam->stParamH264Cbr.u32MinIQp = u32MinIQp;
            pstRcParam->stParamH264Cbr.u32MaxQp = u32MaxQp;
            pstRcParam->stParamH264Cbr.u32MinQp = u32MinQp;
        } else if (enRcMode == VENC_RC_MODE_H264VBR) {
            pstRcParam->stParamH264Vbr.u32MaxIprop = u32MaxIprop;
            pstRcParam->stParamH264Vbr.u32MinIprop = u32MinIprop;
            pstRcParam->stParamH264Vbr.u32MaxIQp = u32MaxIQp;
            pstRcParam->stParamH264Vbr.u32MinIQp = u32MinIQp;
            pstRcParam->stParamH264Vbr.u32MaxQp = u32MaxQp;
            pstRcParam->stParamH264Vbr.u32MinQp = u32MinQp;
            pstRcParam->stParamH264Vbr.s32ChangePos = s32ChangePos;
        } else if (enRcMode == VENC_RC_MODE_H264AVBR) {
            pstRcParam->stParamH264AVbr.u32MaxIprop = u32MaxIprop;
            pstRcParam->stParamH264AVbr.u32MinIprop = u32MinIprop;
            pstRcParam->stParamH264AVbr.u32MaxIQp = u32MaxIQp;
            pstRcParam->stParamH264AVbr.u32MinIQp = u32MinIQp;
            pstRcParam->stParamH264AVbr.u32MaxQp = u32MaxQp;
            pstRcParam->stParamH264AVbr.u32MinQp = u32MinQp;
            pstRcParam->stParamH264AVbr.s32ChangePos = s32ChangePos;
            pstRcParam->stParamH264AVbr.s32MinStillPercent = s32MinStillPercent;
            pstRcParam->stParamH264AVbr.u32MaxStillQP = u32MaxStillQP;
            pstRcParam->stParamH264AVbr.u32MotionSensitivity = u32MotionSensitivity;
            pstRcParam->stParamH264AVbr.s32AvbrFrmLostOpen = s32AvbrFrmLostOpen;
            pstRcParam->stParamH264AVbr.s32AvbrFrmGap = s32AvbrFrmGap;
            pstRcParam->stParamH264AVbr.s32AvbrPureStillThr = s32AvbrPureStillThr;
        } else {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"enRcMode(%d) not support\n", enRcMode);
            return CVI_FAILURE;
        }
    }
    break;
    default:
        APP_PROF_LOG_PRINT(LEVEL_ERROR,"cann't support this enType (%d) in this version!\n", enCodecType);
        return CVI_ERR_VENC_NOT_SUPPORT;
    }

    s32Ret = CVI_VENC_SetRcParam(VencChn, pstRcParam);
    if (s32Ret != CVI_SUCCESS) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "SetRcParam, 0x%X\n", s32Ret);
        return s32Ret;
    }

    return CVI_SUCCESS;
}

static void _Data_Handle(void* pData, void* pArgs)
{
    APP_DATA_CTX_S* pDataCtx = (APP_DATA_CTX_S*)pArgs;
    APP_DATA_PARAM_S* pstDataParam = &pDataCtx->stDataParam;
    APP_VENC_CHN_CFG_S *pstVencChnCfg = (APP_VENC_CHN_CFG_S *)pstDataParam->pParam;
    VENC_CHN VencChn = pstVencChnCfg->VencChn;

    for (int i = 0; i < APP_DATA_COMSUMES_MAX; i++) {
        if (g_Consumes[VencChn][i] != NULL) {
            g_Consumes[VencChn][i](pData, pArgs, g_pUserData[VencChn][i]);
        }
    }
}

static CVI_S32 _Data_Save(void **dst, void *src)
{
    if(dst == NULL || src == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "dst or src is NULL\n");
        return CVI_FAILURE;
    }

    CVI_U32 i = 0;
    VENC_STREAM_S *psrc = (VENC_STREAM_S *)src;
    VENC_STREAM_S *pdst = (VENC_STREAM_S *)malloc(sizeof(VENC_STREAM_S));
    if(pdst == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "pdst malloc failded\n");
        return CVI_FAILURE;
    }
    memcpy(pdst, psrc, sizeof(VENC_STREAM_S));
    pdst->pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * psrc->u32PackCount);
    if(pdst->pstPack == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "pdst->pstPack malloc failded\n");
        goto EXIT1;
    }
    memset(pdst->pstPack, 0, sizeof(VENC_PACK_S) * psrc->u32PackCount);
    for(i = 0; i < psrc->u32PackCount; i++) {
        memcpy(&pdst->pstPack[i], &psrc->pstPack[i], sizeof(VENC_PACK_S));
        pdst->pstPack[i].pu8Addr = (CVI_U8 *)malloc(psrc->pstPack[i].u32Len);
        if(!pdst->pstPack[i].pu8Addr) {
            goto EXIT2;
        }
        memcpy(pdst->pstPack[i].pu8Addr, psrc->pstPack[i].pu8Addr, psrc->pstPack[i].u32Len);
    }

    *dst = (void *)pdst;

    return CVI_SUCCESS;

EXIT2:
    if(pdst->pstPack) {
        for(i = 0;i < psrc->u32PackCount; i++) {
            if(pdst->pstPack[i].pu8Addr) {
                free(pdst->pstPack[i].pu8Addr);
            }
        }
        free(pdst->pstPack);
    }

EXIT1:
    free(pdst);

    return CVI_FAILURE;
}

static CVI_S32 _Data_Free(void **src)
{
    if(src == NULL || *src == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "src is NULL\n");
        return CVI_FAILURE;
    }

    VENC_STREAM_S *psrc = NULL;
    psrc = (VENC_STREAM_S *)*src;

    if(psrc->pstPack) {
        for(CVI_U32 i = 0; i < psrc->u32PackCount; i++) {
            if(psrc->pstPack[i].pu8Addr) {
                free(psrc->pstPack[i].pu8Addr);
            }
        }
        free(psrc->pstPack);
    }
    if(psrc) {
        free(psrc);
    }

    return CVI_SUCCESS;
}

static void* Thread_Streaming_Proc(void* pArgs)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    APP_VENC_CHN_CFG_S* pastVencChnCfg = (APP_VENC_CHN_CFG_S*)pArgs;
    VENC_CHN VencChn = pastVencChnCfg->VencChn;
    CVI_S32 vpssGrp = pastVencChnCfg->VpssGrp;
    CVI_S32 vpssChn = pastVencChnCfg->VpssChn;
    CVI_S32 iTime = GetCurTimeInMsec();

    CVI_CHAR TaskName[64] = { '\0' };
    sprintf(TaskName, "Thread_Venc%d_Proc", VencChn);
    prctl(PR_SET_NAME, TaskName, 0, 0, 0);
    APP_PROF_LOG_PRINT(LEVEL_INFO, "Venc channel_%d start running\n", VencChn);

    pastVencChnCfg->bStart = CVI_TRUE;
    while (pastVencChnCfg->bStart) {
        VIDEO_FRAME_INFO_S stVpssFrame = { 0 };

        if (pastVencChnCfg->enBindMode == VENC_BIND_DISABLE) {
            if (CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, &stVpssFrame, 3000) != CVI_SUCCESS) {
                continue;
            }
            APP_PROF_LOG_PRINT(LEVEL_DEBUG, "VencChn-%d Get Frame takes %u ms \n", VencChn, (GetCurTimeInMsec() - iTime));
            iTime = GetCurTimeInMsec();

            if (pastVencChnCfg->no_need_venc) {
                for (uint32_t i = 0; i < APP_DATA_COMSUMES_MAX; i++) {
                    if (g_Consumes[VencChn][i]) {
                        g_Consumes[VencChn][i](&stVpssFrame, pastVencChnCfg, g_pUserData[VencChn][i]);
                    }
                }
                // release vpss frame
                s32Ret = CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stVpssFrame);
                if (s32Ret != CVI_SUCCESS)
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "vpss release Chn-frame failed with:0x%x\n", s32Ret);
                continue;
            } else if (CVI_VENC_SendFrame(VencChn, &stVpssFrame, 3000) != CVI_SUCCESS) { /* takes 0~1ms */
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "Venc send frame failed with %#x\n", s32Ret);
                s32Ret = CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stVpssFrame);
                if (s32Ret != CVI_SUCCESS)
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "vpss release Chn-frame failed with:0x%x\n", s32Ret);
                continue;
            }
        }

        // vencFd
        CVI_S32 vencFd = CVI_VENC_GetFd(VencChn);
        if (vencFd <= 0) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VENC_GetFd failed with%#x!\n", vencFd);
            break;
        }
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(vencFd, &readFds);

        struct timeval timeoutVal;
        timeoutVal.tv_sec = 0;
        timeoutVal.tv_usec = 80 * 1000;
        iTime = GetCurTimeInMsec();
        s32Ret = select(vencFd + 1, &readFds, NULL, NULL, &timeoutVal);
        if (s32Ret < 0) {
            if (errno == EINTR)
                continue;
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "VencChn(%d) select failed!\n", VencChn);
            break;
        } else if (s32Ret == 0) {
            APP_PROF_LOG_PRINT(LEVEL_DEBUG, "VencChn(%d) select timeout %u ms \n",
                VencChn, (GetCurTimeInMsec() - iTime));
            continue;
        }

        // get stream
        VENC_STREAM_S stStream = { 0 };
        stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * H26X_MAX_NUM_PACKS);
        if (stStream.pstPack == NULL) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "streaming malloc memory failed!\n");
            break;
        }

        ISP_EXP_INFO_S stExpInfo;
        memset(&stExpInfo, 0, sizeof(stExpInfo));
        CVI_ISP_QueryExposureInfo(0, &stExpInfo);
        CVI_S32 timeout = (1000 * 2) / (stExpInfo.u32Fps / 100); // u32Fps = fps * 100
        s32Ret = CVI_VENC_GetStream(VencChn, &stStream, timeout);
        if (pastVencChnCfg->enBindMode == VENC_BIND_DISABLE) {
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stVpssFrame);
        }
        if (s32Ret != CVI_SUCCESS || (0 == stStream.u32PackCount)) {
            APP_PROF_LOG_PRINT(LEVEL_WARN, "CVI_VENC_GetStream, VencChn(%d) cnt(%d), s32Ret = 0x%X timeout:%d %d\n",
                VencChn, stStream.u32PackCount, s32Ret, timeout, stExpInfo.u32Fps);
            goto CONTINUE;
        }

        if ((1 == stStream.u32PackCount) && (stStream.pstPack[0].u32Len > P_MAX_SIZE)) {
            APP_PROF_LOG_PRINT(LEVEL_WARN, "CVI_VENC_GetStream, VencChn(%d) p oversize:%d\n",
                VencChn, stStream.pstPack[0].u32Len);
        } else {
            /* save streaming to LinkList and proc it in another thread */
            s32Ret = app_ipcam_LList_Data_Push(&stStream, g_pDataCtx[VencChn]);
            if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "Venc %d streaming push linklist failed!\n", VencChn);
            }
        }

        s32Ret = CVI_VENC_ReleaseStream(VencChn, &stStream);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_VENC_ReleaseStream, s32Ret = %d\n", s32Ret);
            goto CONTINUE;
        }

    CONTINUE:
        free(stStream.pstPack);
        stStream.pstPack = NULL;
    }

    return (CVI_VOID*)CVI_SUCCESS;
}

static void app_ipcam_VencRemap(void)
{
    APP_PROF_LOG_PRINT(LEVEL_INFO, "[%s](%d) Enter +\n", __func__, __LINE__);
    system("echo 1 > /sys/module/cvi_vc_driver/parameters/addrRemapEn");
    system("echo 256 > /sys/module/cvi_vc_driver/parameters/ARExtraLine");
    system("echo 1 > /sys/module/cvi_vc_driver/parameters/ARMode");
    APP_PROF_LOG_PRINT(LEVEL_INFO, "[%s](%d) Exit -\n", __func__, __LINE__);
}

int app_ipcam_Venc_Init(APP_VENC_CHN_E VencIdx)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    APP_PARAM_SYS_CFG_S *pstSysCfg = app_ipcam_Sys_Param_Get();
    CVI_BOOL bSBMEnable = pstSysCfg->bSBMEnable;
    CVI_S32 iTime;

    APP_PROF_LOG_PRINT(LEVEL_INFO, "Ven init ------------------> start \n");
    
    static pthread_once_t venc_remap_once = PTHREAD_ONCE_INIT;
    pthread_once(&venc_remap_once, app_ipcam_VencRemap);

    for (VENC_CHN s32ChnIdx = 0; s32ChnIdx < g_pstVencCtx->s32VencChnCnt; s32ChnIdx++) {
        APP_VENC_CHN_CFG_S *pstVencChnCfg = &g_pstVencCtx->astVencChnCfg[s32ChnIdx];
        APP_VENC_ROI_CFG_S *pstVencRoiCfg = g_pstVencCtx->astRoiCfg;
        VENC_CHN VencChn = pstVencChnCfg->VencChn;
        if ((!pstVencChnCfg->bEnable) || (pstVencChnCfg->bStart))
            continue;

        if (!((VencIdx >> s32ChnIdx) & 0x01))
            continue;

        APP_PROF_LOG_PRINT(LEVEL_TRACE, "Ven_%d init info\n", VencChn);
        APP_PROF_LOG_PRINT(LEVEL_TRACE, "VpssGrp=%d VpssChn=%d size_W=%d size_H=%d CodecType=%d save_path=%s\n", 
            pstVencChnCfg->VpssGrp, pstVencChnCfg->VpssChn, pstVencChnCfg->u32Width, pstVencChnCfg->u32Height,
            pstVencChnCfg->enType, pstVencChnCfg->SavePath);

        PAYLOAD_TYPE_E enCodecType = pstVencChnCfg->enType;
        VENC_CHN_ATTR_S stVencChnAttr, *pstVencChnAttr = &stVencChnAttr;
        memset(&stVencChnAttr, 0, sizeof(stVencChnAttr));

        /* Venc channel validity check */
        if (VencChn != pstVencChnCfg->VencChn) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"VencChn error %d\n", VencChn);
            goto VENC_EXIT0;
        }

        s32Ret = app_ipcam_Venc_Chn_Attr_Set(&pstVencChnAttr->stVencAttr, pstVencChnCfg);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"media_venc_set_attr [%d] failed with 0x%x\n", VencChn, s32Ret);
            goto VENC_EXIT0;
        }

        s32Ret = app_ipcam_Venc_Gop_Attr_Set(&pstVencChnAttr->stGopAttr, pstVencChnCfg);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"media_venc_set_gop [%d] failed with 0x%x\n", VencChn, s32Ret);
            goto VENC_EXIT0;
        }

        s32Ret = app_ipcam_Venc_Rc_Attr_Set(&pstVencChnAttr->stRcAttr, pstVencChnCfg);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"media_venc_set_rc_attr [%d] failed with 0x%x\n", VencChn, s32Ret);
            goto VENC_EXIT0;
        }

        app_ipcam_Venc_Attr_Check(pstVencChnAttr);

        APP_PROF_LOG_PRINT(LEVEL_DEBUG,"u32Profile [%d]\n", pstVencChnAttr->stVencAttr.u32Profile);

        iTime = GetCurTimeInMsec();
        s32Ret = CVI_VENC_CreateChn(VencChn, pstVencChnAttr);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"CVI_VENC_CreateChn [%d] failed with 0x%x\n", VencChn, s32Ret);
            goto VENC_EXIT1;
        }
        APP_PROF_LOG_PRINT(LEVEL_WARN, "venc_%d CVI_VENC_CreateChn takes %u ms \n", VencChn, (GetCurTimeInMsec() - iTime));

        if ((enCodecType == PT_H265) || (enCodecType == PT_H264)) {
            if (enCodecType == PT_H264)
            {
                s32Ret = app_ipcam_Venc_H264Entropy_Set(VencChn);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR,"app_ipcam_Venc_H264Entropy_Set [%d] failed with 0x%x\n", VencChn, s32Ret);
                    goto VENC_EXIT1;
                }
                s32Ret = app_ipcam_Venc_H264Trans_Set(VencChn);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR,"app_ipcam_Venc_H264Trans_Set [%d] failed with 0x%x\n", VencChn, s32Ret);
                    goto VENC_EXIT1;
                }
                s32Ret = app_ipcam_Venc_H264Vui_Set(VencChn);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR,"app_ipcam_Venc_H264Vui_Set [%d] failed with 0x%x\n", VencChn, s32Ret);
                    goto VENC_EXIT1;
                }
                s32Ret = app_ipcam_Venc_Roi_Set(VencChn, pstVencRoiCfg);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR,"app_ipcam_Venc_Roi_Set [%d] failed with 0x%x\n", VencChn, s32Ret);
                    goto VENC_EXIT1;
                }
            } else {
                s32Ret = app_ipcam_Venc_H265Trans_Set(VencChn);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR,"app_ipcam_Venc_H265Trans_Set [%d] failed with 0x%x\n", VencChn, s32Ret);
                    goto VENC_EXIT1;
                }
                s32Ret = app_ipcam_Venc_H265Vui_Set(VencChn);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR,"app_ipcam_Venc_H265Vui_Set [%d] failed with 0x%x\n", VencChn, s32Ret);
                    goto VENC_EXIT1;
                }
            }
            s32Ret = app_ipcam_Venc_Rc_Param_Set(VencChn, enCodecType, pstVencChnCfg->enRcMode, &pstVencChnCfg->stRcParam);
                if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR,"Venc_%d RC Param Set failed with 0x%x\n", VencChn, s32Ret);
                goto VENC_EXIT1;
            }

            s32Ret = app_ipcam_Venc_FrameLost_Set(VencChn, &pstVencChnCfg->stFrameLostCtrl);
                if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR,"Venc_%d RC frame lost control failed with 0x%x\n", VencChn, s32Ret);
                goto VENC_EXIT1;
            }
        } else if (enCodecType == PT_JPEG) {
            s32Ret = app_ipcam_Venc_Jpeg_Param_Set(VencChn, &pstVencChnCfg->stJpegCodecParam);
                if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR,"Venc_%d JPG Param Set failed with 0x%x\n", VencChn, s32Ret);
                goto VENC_EXIT1;
            }
        }

        if (bSBMEnable) {
            if ((s32ChnIdx != 0) && (pstVencChnCfg->enBindMode != VENC_BIND_DISABLE)) {
                s32Ret = CVI_SYS_Bind(&pstVencChnCfg->astChn[0], &pstVencChnCfg->astChn[1]);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_Bind failed with %#x\n", s32Ret);
                    goto VENC_EXIT1;
                }
            }
        } else {
            if (pstVencChnCfg->enBindMode != VENC_BIND_DISABLE) {
                s32Ret = CVI_SYS_Bind(&pstVencChnCfg->astChn[0], &pstVencChnCfg->astChn[1]);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_Bind failed with %#x\n", s32Ret);
                    goto VENC_EXIT1;
                }
            }
        }

        { // init LList
            APP_DATA_PARAM_S stDataParam = {0};
            stDataParam.pParam = pstVencChnCfg;
            stDataParam.fpDataSave = _Data_Save;
            stDataParam.fpDataFree = _Data_Free;
            stDataParam.fpDataHandle = _Data_Handle;
            s32Ret = app_ipcam_LList_Data_Init(&g_pDataCtx[VencChn], &stDataParam);
            if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR, "Link list data init failed with %#x\n", s32Ret);
                goto VENC_EXIT1;
            }
        }
    }

    APP_PROF_LOG_PRINT(LEVEL_INFO, "Ven init ------------------> done \n");

    return CVI_SUCCESS;

VENC_EXIT1:
    for (VENC_CHN s32ChnIdx = 0; s32ChnIdx < g_pstVencCtx->s32VencChnCnt; s32ChnIdx++) {
        CVI_VENC_ResetChn(g_pstVencCtx->astVencChnCfg[s32ChnIdx].VencChn);
        CVI_VENC_DestroyChn(g_pstVencCtx->astVencChnCfg[s32ChnIdx].VencChn);
    }

VENC_EXIT0:
    app_ipcam_Vpss_DeInit();
    app_ipcam_Vi_DeInit();
    app_ipcam_Sys_DeInit();

    return s32Ret;
}

int app_ipcam_Venc_Start(APP_VENC_CHN_E VencIdx)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    APP_PARAM_SYS_CFG_S *pstSysCfg = app_ipcam_Sys_Param_Get();
    CVI_BOOL bSBMEnable = pstSysCfg->bSBMEnable;

    for (VENC_CHN s32ChnIdx = 0; s32ChnIdx < g_pstVencCtx->s32VencChnCnt; s32ChnIdx++) {
        APP_VENC_CHN_CFG_S *pstVencChnCfg = &g_pstVencCtx->astVencChnCfg[s32ChnIdx];
        VENC_CHN VencChn = pstVencChnCfg->VencChn;
        if ((!pstVencChnCfg->bEnable) || (pstVencChnCfg->bStart))
            continue;

        if (!((VencIdx >> s32ChnIdx) & 0x01))
            continue;

        VENC_RECV_PIC_PARAM_S stRecvParam = {0};
        stRecvParam.s32RecvPicNum = -1;

        APP_CHK_RET(CVI_VENC_StartRecvFrame(VencChn, &stRecvParam), "Start recv frame");
       
        if (bSBMEnable) {
            if ((s32ChnIdx == 0) && (pstVencChnCfg->enBindMode != VENC_BIND_DISABLE)) {
                s32Ret = CVI_SYS_Bind(&pstVencChnCfg->astChn[0], &pstVencChnCfg->astChn[1]);
                if (s32Ret != CVI_SUCCESS) {
                    APP_PROF_LOG_PRINT(LEVEL_ERROR, "CVI_SYS_Bind failed with %#x\n", s32Ret);
                }
            }
        }

        pthread_attr_t pthread_attr;
        pthread_attr_init(&pthread_attr);

        pfp_task_entry fun_entry = NULL;
        struct sched_param param;
        param.sched_priority = 80;
        pthread_attr_setschedpolicy(&pthread_attr, SCHED_RR);
        pthread_attr_setschedparam(&pthread_attr, &param);
        pthread_attr_setinheritsched(&pthread_attr, PTHREAD_EXPLICIT_SCHED);
        fun_entry = Thread_Streaming_Proc;

        g_Venc_pthread[VencChn] = 0;
        s32Ret = pthread_create(
            &g_Venc_pthread[VencChn],
            &pthread_attr,
            fun_entry,
            (CVI_VOID*)pstVencChnCfg);
        if (s32Ret) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR, "[Chn %d]pthread_create failed:0x%x\n", VencChn, s32Ret);
            return CVI_FAILURE;
        }
    }

    return CVI_SUCCESS;
}

int app_ipcam_Venc_Stop(APP_VENC_CHN_E VencIdx)
{
    CVI_S32 s32Ret;

    APP_PROF_LOG_PRINT(LEVEL_INFO, "Venc Count=%d and will stop VencChn=0x%x\n", g_pstVencCtx->s32VencChnCnt, VencIdx);
    for (VENC_CHN s32ChnIdx = 0; s32ChnIdx < g_pstVencCtx->s32VencChnCnt; s32ChnIdx++) {
        APP_VENC_CHN_CFG_S *pstVencChnCfg = &g_pstVencCtx->astVencChnCfg[s32ChnIdx];
        VENC_CHN VencChn = pstVencChnCfg->VencChn;

        if (!((VencIdx >> s32ChnIdx) & 0x01))
            continue;

        if (!pstVencChnCfg->bStart)
            continue;
        pstVencChnCfg->bStart = CVI_FALSE;

        if (g_Venc_pthread[VencChn] != 0) {
            pthread_join(g_Venc_pthread[VencChn], CVI_NULL);
            APP_PROF_LOG_PRINT(LEVEL_WARN, "Venc_%d Streaming Proc done \n", VencChn);
            g_Venc_pthread[VencChn] = 0;
        }

        if (pstVencChnCfg->enBindMode != VENC_BIND_DISABLE) {
            s32Ret = CVI_SYS_UnBind(&pstVencChnCfg->astChn[0], &pstVencChnCfg->astChn[1]);
            if (s32Ret != CVI_SUCCESS) {
                APP_PROF_LOG_PRINT(LEVEL_ERROR,"CVI_SYS_UnBind failed with %#x\n", s32Ret);
                return s32Ret;
            }
        }

        s32Ret = CVI_VENC_StopRecvFrame(VencChn);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"CVI_VENC_StopRecvFrame vechn[%d] failed with %#x\n", VencChn, s32Ret);
            return s32Ret;
        }

        s32Ret = CVI_VENC_ResetChn(VencChn);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"CVI_VENC_ResetChn vechn[%d] failed with %#x\n", VencChn, s32Ret);
            return s32Ret;
        }

        s32Ret = CVI_VENC_DestroyChn(VencChn);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"CVI_VENC_DestroyChn vechn[%d] failed with %#x\n", VencChn, s32Ret);
            return s32Ret;
        }

        s32Ret = app_ipcam_LList_Data_DeInit(&g_pDataCtx[VencChn]);
        if (s32Ret != CVI_SUCCESS) {
            APP_PROF_LOG_PRINT(LEVEL_ERROR,"vechn[%d] LList Data Cache DeInit failed with %#x\n", VencChn, s32Ret);
            return s32Ret;
        }
    }

    return CVI_SUCCESS;
}



// video.c

static bool is_started = false;

static int setVbPool(video_ch_index_t ch, const video_ch_param_t* param, uint32_t u32BlkCnt) {
    APP_PARAM_SYS_CFG_S* sys = app_ipcam_Sys_Param_Get();

    if (ch >= sys->vb_pool_num) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "ch(%d) > vb_pool_num(%d)\n", ch, sys->vb_pool_num);
        return -1;
    }
    if (param == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "param is null\n");
        return -1;
    }

    APP_PARAM_VB_CFG_S* vb = &sys->vb_pool[ch];
    vb->bEnable            = 1;
    vb->width              = param->width;
    vb->height             = param->height;
    vb->fmt                = (param->format == VIDEO_FORMAT_RGB888) ? PIXEL_FORMAT_RGB_888 : PIXEL_FORMAT_NV21;
    vb->vb_blk_num         = u32BlkCnt;

    return 0;
}

static int setGrpChn(int grp, video_ch_index_t ch, const video_ch_param_t* param) {
    APP_PARAM_VPSS_CFG_T* vpss = app_ipcam_Vpss_Param_Get();

    if (grp >= vpss->u32GrpCnt) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "grp(%d) > u32GrpCnt(%d)\n", grp, vpss->u32GrpCnt);
        return -1;
    }
    if (ch >= VIDEO_CH_MAX) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "ch(%d) > VIDEO_CH_MAX(%d)\n", ch, VIDEO_CH_MAX);
        return -1;
    }
    if (param == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "param is null\n");
        return -1;
    }

    APP_VPSS_GRP_CFG_T* pgrp  = &vpss->astVpssGrpCfg[grp];
    pgrp->abChnEnable[ch]     = 1;
    pgrp->aAttachEn[ch]       = 1;
    VPSS_CHN_ATTR_S* vpss_chn = &pgrp->astVpssChnAttr[ch];
    vpss_chn->u32Width        = param->width;
    vpss_chn->u32Height       = param->height;
    vpss_chn->enPixelFormat   = (param->format == VIDEO_FORMAT_RGB888) ? PIXEL_FORMAT_RGB_888 : PIXEL_FORMAT_NV21;

    return 0;
}

static int setVencChn(video_ch_index_t ch, const video_ch_param_t* param) {
    APP_PARAM_VENC_CTX_S* venc = app_ipcam_Venc_Param_Get();

    if (ch >= venc->s32VencChnCnt) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "ch(%d) > u32ChnCnt(%d)\n", ch, venc->s32VencChnCnt);
        return -1;
    }

    if (param == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "param is null\n");
        return -1;
    }

    PAYLOAD_TYPE_E enType = PT_JPEG;
    if (VIDEO_FORMAT_H264 == param->format) {
        enType = PT_H264;
    } else if (VIDEO_FORMAT_H265 == param->format) {
        enType = PT_H265;
    }
    app_ipcam_Param_setVencChnType(ch, enType);
    APP_VENC_CHN_CFG_S* pvchn = &venc->astVencChnCfg[ch];
    pvchn->bEnable            = 1;
    pvchn->u32Width           = param->width;
    pvchn->u32Height          = param->height;
    pvchn->u32DstFrameRate    = param->fps;

    if ((VIDEO_FORMAT_RGB888 == param->format) || (VIDEO_FORMAT_NV21 == param->format)) {
        pvchn->no_need_venc = 1;
    }

    return 0;
}

int initVideo(bool use_venc) {
    APP_CHK_RET(app_ipcam_Param_Load(use_venc), "load global parameter");

    return 0;
}

int deinitVideo(bool stop_venc) {
    if (is_started) {
        // Skip venc deinitialization since we're not using it
        APP_CHK_RET(app_ipcam_Vpss_DeInit(), "Vpss DeInit");
        APP_CHK_RET(app_ipcam_Vi_DeInit(), "Vi DeInit");
        APP_CHK_RET(app_ipcam_Sys_DeInit(), "System DeInit");
        is_started = false;
    }

    if (stop_venc) {
        APP_CHK_RET(app_ipcam_Venc_Stop(APP_VENC_ALL), "stop video processing");
    }
    
    return CVI_SUCCESS;
}

int startVideo(bool start_venc) {
    /* init modules include <Peripheral; Sys; VI; VB; OSD; Venc; AI; Audio; etc.> */
    APP_CHK_RET(app_ipcam_Sys_Init(), "init systerm");
    APP_CHK_RET(app_ipcam_Vi_Init(), "init vi module");
    APP_CHK_RET(app_ipcam_Vpss_Init(), "init vpss module");

    if (start_venc) {
        APP_CHK_RET(app_ipcam_Venc_Init(APP_VENC_ALL), "init video encode");
        APP_CHK_RET(app_ipcam_Venc_Start(APP_VENC_ALL), "start video processing");
    }

    is_started = true;
    
    return 0;
}

int setupVideo(video_ch_index_t ch, const video_ch_param_t* param, bool setup_venc) {
    if (ch >= VIDEO_CH_MAX) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "video ch(%d) index is out of range\n", ch);
        return -1;
    }
    
    if (param == NULL) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "video ch(%d) param is null\n", ch);
        return -1;
    }
    
    if (param->format >= VIDEO_FORMAT_COUNT) {
        APP_PROF_LOG_PRINT(LEVEL_ERROR, "video ch(%d) format(%d) is not support\n", ch, param->format);
        return -1;
    }

    // Setup only channel 0
    setVbPool(ch, param, 2);
    setGrpChn(0, ch, param);
    
    if (setup_venc) {
        setVencChn(ch, param);
    } else {
        //setVencChn(ch, NULL);
        // pvchn->no_need_venc = 1;
    }

    return 0;
}

int registerVideoFrameHandler(video_ch_index_t ch, int index, pfpDataConsumes handler, void* pUserData) {
    app_ipcam_Venc_Consumes_Set(ch, index, handler, pUserData);
    return 0;
}



static bool convert_to_opencv_mat(VIDEO_FRAME_INFO_S* VpssFrame, cv::Mat &outputImage) {
    //std::cout << "convert_to_opencv_mat" << std::endl;

    VIDEO_FRAME_S* f = &VpssFrame->stVFrame;

    // Print debug info
    //std::cout << "Frame info: " << f->u32Width << "x" << f->u32Height 
    //          << ", Pixel format: " << f->enPixelFormat << std::endl;
    //std::cout << "Frame plane lengths: " << f->u32Length[0] << ", " 
    //          << f->u32Length[1] << ", " << f->u32Length[2] << std::endl;


    // Process frame based on its pixel format
    bool success = false;
    
    // Map all planes of physical memory to virtual memory
    for (uint32_t i = 0; i < 3; i++) {
        if (f->u32Length[i]) {
            f->pu8VirAddr[i] = (CVI_U8*)CVI_SYS_Mmap(f->u64PhyAddr[i], f->u32Length[i]);
            if (!f->pu8VirAddr[i]) {
                std::cerr << "Memory mapping failed for plane " << i << std::endl;
                // Unmap any previously mapped planes
                for (uint32_t j = 0; j < i; j++) {
                    if (f->pu8VirAddr[j]) {
                        CVI_SYS_Munmap(f->pu8VirAddr[j], f->u32Length[j]);
                    }
                }
                return false;
            }
        }
    }

    // Handle different pixel formats
    switch (f->enPixelFormat) {
        case PIXEL_FORMAT_RGB_888: {
            // Create OpenCV Mat for RGB data
            cv::Mat rgbImage(f->u32Height, f->u32Width, CV_8UC3, f->pu8VirAddr[0]);
            // Convert RGB to BGR (OpenCV uses BGR by default)
            cv::cvtColor(rgbImage, outputImage, cv::COLOR_RGB2BGR);
            success = true;
            break;
        }
        case PIXEL_FORMAT_NV21: {
            // For NV21 format (Y plane followed by interleaved VU plane)
            cv::Mat yuvImg(f->u32Height * 3 / 2, f->u32Width, CV_8UC1);
            
            // Copy Y plane
            if (f->u32Length[0] > 0 && f->pu8VirAddr[0]) {
                memcpy(yuvImg.data, f->pu8VirAddr[0], f->u32Width * f->u32Height);
            }
            
            // Copy UV plane
            if (f->u32Length[1] > 0 && f->pu8VirAddr[1]) {
                memcpy(yuvImg.data + (f->u32Width * f->u32Height), 
                       f->pu8VirAddr[1], f->u32Width * f->u32Height / 2);
            }
            
            // Convert NV21 to BGR
            cv::cvtColor(yuvImg, outputImage, cv::COLOR_YUV2BGR_NV21);
            success = true;
            break;
        }
        case PIXEL_FORMAT_YUV_PLANAR_420: {
            // For YUV420 planar format (three separate planes for Y, U, V)
            cv::Mat yPlane(f->u32Height, f->u32Width, CV_8UC1, f->pu8VirAddr[0]);
            cv::Mat uPlane(f->u32Height/2, f->u32Width/2, CV_8UC1, f->pu8VirAddr[1]);
            cv::Mat vPlane(f->u32Height/2, f->u32Width/2, CV_8UC1, f->pu8VirAddr[2]);
            
            // Create and populate YUV planes vector
            std::vector<cv::Mat> yuv;
            yuv.push_back(yPlane);
            
            // Resize U and V planes to match Y plane size
            cv::Mat uResized, vResized;
            cv::resize(uPlane, uResized, yPlane.size());
            cv::resize(vPlane, vResized, yPlane.size());
            yuv.push_back(uResized);
            yuv.push_back(vResized);
            
            // Convert YUV to BGR
            cv::merge(yuv, outputImage);
            cv::cvtColor(outputImage, outputImage, cv::COLOR_YUV2BGR);
            success = true;
            break;
        }
        case PIXEL_FORMAT_YUV_400: {
            // Grayscale format (Y plane only)
            cv::Mat yuvImage(f->u32Height, f->u32Width, CV_8UC1, f->pu8VirAddr[0]);
            outputImage = yuvImage.clone();
            success = true;
            break;
        }
        default:
            std::cerr << "Unsupported pixel format: " << f->enPixelFormat << std::endl;
            success = false;
            break;
    }

    // Unmap all memory
    for (uint32_t i = 0; i < 3; i++) {
        if (f->pu8VirAddr[i]) {
            CVI_SYS_Munmap(f->pu8VirAddr[i], f->u32Length[i]);
            f->pu8VirAddr[i] = NULL;
        }
    }

    return success;
}


bool getVideoFrame(video_ch_index_t ch, cv::Mat &frame, int timeout_ms) {
    // Define VPSS group and channel for frame capture
    VPSS_GRP VpssGrp = 0;  // Use first VPSS group
    VPSS_CHN VpssChn = 0;  // Use first VPSS channel

    // Try to get a frame from VPSS with 1000ms timeout
    VIDEO_FRAME_INFO_S stVideoFrame;
    CVI_S32 s32Ret = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, &stVideoFrame, timeout_ms);
    
    if (s32Ret == CVI_SUCCESS) {
        // Process and save the captured frame
        bool success = convert_to_opencv_mat(&stVideoFrame, frame);
        
        // Release the frame back to the system
        CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, &stVideoFrame);
        
        if (success) {
            //std::cout << "Frame captured successfully" << std::endl;
            return true;
        } else {
            std::cerr << "Failed to save frame" << std::endl;
            return false;
        }
    } else {
        std::cerr << "Failed to get frame" << std::endl;
        return false;
    }  
}

int cvi_system_setVbPool(video_ch_index_t ch, const video_ch_param_t* param, uint32_t u32BlkCnt) {
    return setVbPool(ch, param, u32BlkCnt);
}

int cvi_system_Sys_Init(void) {
    return app_ipcam_Sys_Init();
}

int cvi_system_Sys_DeInit(void) {
    return app_ipcam_Sys_DeInit();
}
