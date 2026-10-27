#ifndef DABOM_H
#define DABOM_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#endif

/* Single source of truth for app version (keep DABOM_VERSION_W in sync). */
#define DABOM_VERSION   "0.0.2"
#define DABOM_VERSION_W L"0.0.2"
#define DABOM_TITLE_W   L"DaBom " DABOM_VERSION_W

#define DABOM_MAX_PATH 1024
#define DABOM_MAX_FILES 8192
#define DABOM_THUMB 160
#define DABOM_THUMB_PAD 12
#define DABOM_TOOLBAR_H 48
#define DABOM_STATUS_H 28
#define DABOM_SLIDE_MS 3000
#define DABOM_ZOOM_MIN 0.1
#define DABOM_ZOOM_MAX 8.0
#define DABOM_UI_FONT_PT_DEFAULT 11
#define DABOM_UI_FONT_PT_MIN 9
#define DABOM_UI_FONT_PT_MAX 18
/* Brand hero ≈ 3.5× UI pt (scales with ui_font_size_pt). */
#define DABOM_BRAND_FONT_PT_MULT 7
#define DABOM_BRAND_FONT_PT_DIV  2

enum {
    ID_BTN_FOLDER = 1001,
    ID_BTN_FILES,
    ID_BTN_PREV,
    ID_BTN_NEXT,
    ID_BTN_ZOOM_IN,
    ID_BTN_ZOOM_OUT,
    ID_BTN_FIT,
    ID_BTN_ACTUAL,
    ID_BTN_SLIDE,
    ID_BTN_FONT_DEC,
    ID_BTN_FONT_INC,
    ID_TIMER_SLIDE = 2001
};

typedef enum {
    VIEW_EMPTY = 0,
    VIEW_GRID,
    VIEW_FULL
} ViewMode;

/* Windows Apps color mode palette (independent of Cursor). */
typedef struct {
    COLORREF bg;
    COLORREF surface;
    COLORREF border;
    COLORREF text;
    COLORREF muted;
    COLORREF accent;
    COLORREF accent_dk;
    COLORREF grid_bg;
    int dark; /* 1 = dark Apps theme */
} DabomPalette;

typedef struct {
    wchar_t path[DABOM_MAX_PATH];
    HBITMAP thumb;
    int thumb_w;
    int thumb_h;
} ImageEntry;

typedef struct {
    HWND hwnd;
    HWND toolbar;
    HWND status;
    HFONT font_ui;
    HFONT font_brand;
    HFONT font_btn;
    HBRUSH br_status; /* status-bar background; recreated on theme change */
    IWICImagingFactory *wic;

    DabomPalette palette;

    ImageEntry *files;
    int file_count;
    int file_cap;
    int current;
    int scroll_y;
    int content_h;

    ViewMode mode;
    HBITMAP full_bmp;
    UINT full_w;
    UINT full_h;
    double zoom;
    int fit_mode; /* 1 = fit to window */
    int pan_x;
    int pan_y;
    int dragging;
    int drag_ox;
    int drag_oy;
    int slideshow;

    wchar_t last_folder[DABOM_MAX_PATH];
    wchar_t config_path[DABOM_MAX_PATH];
    int ui_font_size_pt; /* independent UI size; default 11 pt — not Cursor */
} AppState;

/* image.c */
int dabom_wic_init(AppState *app);
void dabom_wic_shutdown(AppState *app);
HBITMAP dabom_load_bitmap(AppState *app, const wchar_t *path, UINT *out_w, UINT *out_h, UINT max_side);
void dabom_free_bitmap(HBITMAP *pbmp);

/* files.c */
void dabom_clear_files(AppState *app);
int dabom_add_file(AppState *app, const wchar_t *path);
int dabom_scan_folder(AppState *app, const wchar_t *folder);
int dabom_is_image_ext(const wchar_t *path);
void dabom_ensure_thumb(AppState *app, int index);
void dabom_load_current_full(AppState *app);

/* config.c */
void dabom_config_path(AppState *app);
void dabom_load_settings(AppState *app);
void dabom_save_settings(AppState *app);

/* fonts (main.c) — true point size via MulDiv(pt, LOGPIXELSY, 72) */
void dabom_create_fonts(AppState *app);
void dabom_apply_fonts(AppState *app);
void dabom_set_ui_font_pt(AppState *app, int pt);

/* theme (main.c) — follows Windows Apps color mode (not Cursor) */
int dabom_apps_use_dark_mode(void);
void dabom_theme_apply(AppState *app, int force);

/* ui helpers in main.c */
void dabom_set_status(AppState *app, const wchar_t *text);
void dabom_update_chrome(AppState *app);
void dabom_open_folder_dialog(AppState *app);
void dabom_open_files_dialog(AppState *app);
void dabom_goto_index(AppState *app, int index);
void dabom_next(AppState *app, int delta);
void dabom_set_mode(AppState *app, ViewMode mode);
void dabom_toggle_slideshow(AppState *app);
void dabom_zoom_by(AppState *app, double factor);
void dabom_fit(AppState *app);
void dabom_actual_size(AppState *app);

#endif /* DABOM_H */
