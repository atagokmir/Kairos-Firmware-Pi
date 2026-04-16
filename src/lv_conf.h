/**
 * @file lv_conf.h
 * Kairos Pi 5 — LVGL v9 configuration
 */

#if 1 /* Enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 32   /* 24-bit RGB + 8-bit alpha — ideal for SDL2 */

/*====================
    MEMORY SETTINGS
 *====================*/
#define LV_MEM_SIZE (512U * 1024U)   /* 512 KB */
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC   malloc
#define LV_MEM_POOL_FREE    free

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD 33   /* 30 fps */
#define LV_DPI_DEF 130

/*===================
   DRAW SETTINGS
 *==================*/
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

/*====================
 * LOGGING
 *====================*/
#define LV_USE_LOG 0

/*====================
 * ASSERT
 *====================*/
#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE  0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ    0

/*====================
 * OTHERS
 *====================*/
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0

/*===================
 *  FONT USAGE
 *==================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " "
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 *  WIDGETS
 *================*/
#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_BAR        1
#define LV_USE_BUTTON     0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      1
#define LV_USE_CHECKBOX   0
#define LV_USE_COLORWHEEL 0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGE      0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LABEL      1
#define LV_USE_LED        1
#define LV_USE_LINE       1
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

/*==================
 * THEMES
 *================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/*==================
 * LAYOUTS
 *================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*==================
 *   DRIVERS
 *================*/

/* SDL2 — Mac simülatör */
#ifdef KAIROS_SDL2
  #define LV_USE_SDL 1
  #define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
  #define LV_SDL_FULLSCREEN 0
  #define LV_SDL_DIRECT_EXIT 1
  #define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#else
  #define LV_USE_SDL 0
#endif

/* Linux framebuffer — Pi 5 */
#ifdef KAIROS_FBDEV
  #define LV_USE_LINUX_FBDEV 1
  #define LV_LINUX_FBDEV_BSD 0
  #define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
  #define LV_LINUX_FBDEV_BUFFER_COUNT 0
  #define LV_LINUX_FBDEV_BUFFER_SIZE 60
#else
  #define LV_USE_LINUX_FBDEV 0
#endif

/* evdev touch — Pi 5 (ileride) */
#define LV_USE_EVDEV 0

/* Diğer sürücüler kapalı */
#define LV_USE_WINDOWS       0
#define LV_USE_OPENGLES      0
#define LV_USE_X11           0
#define LV_USE_LINUX_DRM     0
#define LV_USE_TFT_ESPI      0
#define LV_USE_NUTTX         0

#endif /* LV_CONF_H */

#endif /* Enable content */
