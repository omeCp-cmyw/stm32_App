/********************************************************************************
**
** 文件名:     drv_lcd.h
** 版权所有:   无
** 文件描述:   该模块主要实现LCD驱动接口定义
**
*********************************************************************************/


#ifndef __DRV_LCD_H
#define __DRV_LCD_H

#include <stdint.h>

/* RGB565 常用颜色定义（与NT35510驱动一致） */
#define WHITE                         0xFFFF  /* 白色 */
#define BLACK                         0x0000  /* 黑色 */
#define GREY                          0xF7DE  /* 灰色 */
#define BLUE                          0x001F  /* 蓝色 */
#define RED                           0xF800  /* 红色 */
#define MAGENTA                       0xF81F  /* 品红色 */
#define GREEN                         0x07E0  /* 绿色 */
#define CYAN                          0x7FFF  /* 青色 */
#define YELLOW                        0xFFE0  /* 黄色 */

/* LCD像素格式 */
typedef enum {
    LCD_PIXEL_FORMAT_RGB565 = 0,
    LCD_PIXEL_FORMAT_RGB888,
    LCD_PIXEL_FORMAT_ARGB8888,
    LCD_PIXEL_FORMAT_MAX
} LCD_PixelFormat_e;

/* LCD区域 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} LCD_Region_t;

/* LCD驱动接口 */
int drv_lcd_init(void);
int drv_lcd_deinit(void);
int drv_lcd_clear(uint32_t color);
int drv_lcd_fill_rect(LCD_Region_t *region, uint32_t color);
int drv_lcd_draw_pixel(uint16_t x, uint16_t y, uint32_t color);
int drv_lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint32_t color);
int drv_lcd_set_backlight(uint8_t brightness);
int drv_lcd_get_width(void);
int drv_lcd_get_height(void);

#endif /* __DRV_LCD_H */
