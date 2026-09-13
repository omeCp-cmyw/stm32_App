/********************************************************************************
**
** 文件名:     drv_camera.h
** 版权所有:   无
** 文件描述:   该模块主要实现摄像头驱动接口定义
**
*********************************************************************************/


#ifndef __DRV_CAMERA_H
#define __DRV_CAMERA_H

#include <stdint.h>

/* 分辨率 */
typedef enum {
    RESOLUTION_320x240 = 0,
    RESOLUTION_640x480,
    RESOLUTION_800x600,
    RESOLUTION_1024x768,
    RESOLUTION_MAX
} Resolution_e;

/* 图像格式 */
typedef enum {
    FORMAT_RGB565 = 0,
    FORMAT_RGB888,
    FORMAT_JPEG,
    FORMAT_MAX
} ImageFormat_e;

/* 摄像头帧数据 */
typedef struct {
    uint8_t *frame_buffer;
    uint32_t frame_size;
    uint16_t width;
    uint16_t height;
    ImageFormat_e format;
    uint32_t timestamp;
} CameraFrame_t;

/* 摄像头驱动接口 */
int drv_camera_init(void);
int drv_camera_deinit(void);
int drv_camera_capture(CameraFrame_t *frame);
int drv_camera_set_resolution(Resolution_e resolution);
int drv_camera_set_format(ImageFormat_e format);
int drv_camera_start_stream(void);
int drv_camera_stop_stream(void);

#endif /* __DRV_CAMERA_H */
