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
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
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
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "msimg32.lib")
#endif

#define DABOM_VERSION   "0.0.8"
#define DABOM_VERSION_W L"0.0.8"
#define DABOM_TITLE_W   L"DaBom " DABOM_VERSION_W

/* Win32 resources (dabom.rc) */
#define IDI_APPICON     101
#define IDR_LOGO_LIGHT  201
#define IDR_LOGO_DARK   202

#define DABOM_MAX_PATH 1024
#define DABOM_MAX_FILES 8192
#define DABOM_MAX_BOOKMARKS 64
#define DABOM_THUMB 160
#define DABOM_THUMB_PAD 12
#define DABOM_STATUS_ID 3001
#define DABOM_SLIDE_MS 3000
#define DABOM_ZOOM_MIN 0.1
#define DABOM_ZOOM_MAX 8.0
#define DABOM_BRAND_FONT_PT_MULT 7
#define DABOM_BRAND_FONT_PT_DIV  2
/* Layout constants are authored at 96 DPI; scale with DABOM_SCALE. */
#define DABOM_DPI_DEFAULT 96
#define DABOM_SCALE(app, v) \
    MulDiv((int)(v), (int)(((app)->dpi) ? (app)->dpi : DABOM_DPI_DEFAULT), DABOM_DPI_DEFAULT)

enum {
    ID_BTN_PREV = 1002,
    ID_BTN_NEXT,
    ID_BTN_ZOOM_OUT,
    ID_BTN_ZOOM_IN,
    ID_BTN_FIT,
    ID_BTN_ACTUAL,
    ID_BTN_SLIDE,
    ID_MENU_OPEN_FOLDER = 1101,
    ID_MENU_OPEN_FILES,
    ID_MENU_FILE_INFO,
    ID_MENU_ABOUT,
    ID_MENU_EXIT,
    /* Context — view / edit (ref 1) */
    ID_CTX_FULLSCREEN = 1201,
    ID_CTX_CONTINUOUS,
    ID_CTX_SMOOTH,
    ID_CTX_COPY,
    ID_CTX_SAVE_AS,
    ID_CTX_ROTATE,
    ID_CTX_ROTATE_CCW,
    ID_CTX_RESIZE,
    ID_CTX_CONVERT,
    ID_CTX_PRINT,
    ID_CTX_PIXEL,
    ID_CTX_CAMERA,
    ID_CTX_PROPS,
    /* Context — layout submenu (ref 1+3) */
    ID_CTX_LAYOUT_FIT_LARGE = 1251, /* 큰 이미지만 화면에 맞춤 */
    ID_CTX_LAYOUT_FIT,              /* 화면에 맞춤 / menubar 맞춤 */
    ID_CTX_LAYOUT_WIDTH,            /* 화면 너비에 맞춤 */
    ID_CTX_LAYOUT_HEIGHT,           /* 화면 높이에 맞춤 */
    ID_CTX_LAYOUT_ACTUAL_KEEP,      /* 원본크기로 계속 보기 */
    ID_CTX_LAYOUT_ZOOM_KEEP,        /* 현재 배율 유지 */
    ID_CTX_LAYOUT_STRETCH,          /* 창에 늘리기 */
    ID_CTX_LAYOUT_ACTUAL,           /* 원본크기 (one-shot, menubar) */
    ID_CTX_PAGE_ONE,                /* 한장보기 */
    ID_CTX_PAGE_TWO,                /* 두장보기 */
    ID_CTX_PAGE_FOUR,               /* 네장보기 */
    /* Context — ref 2 unique */
    ID_CTX_DELETE = 1301,
    ID_CTX_SORT_NAME,
    ID_CTX_SORT_DATE,
    ID_CTX_SORT_SIZE,
    ID_CTX_HDR,
    ID_CTX_BOOKMARK_ADD,
    ID_CTX_BOOKMARK_JUMP,
    ID_CTX_PASTE,
    ID_CTX_EXPLORER,
    ID_CTX_EDIT_APP,
    ID_CTX_LIBRARY,
    ID_CTX_WALLPAPER,
    ID_CTX_IMG_ROTATE,
    ID_CTX_IMG_RESIZE,
    ID_CTX_IMG_CONVERT,
    ID_CTX_BOOKMARK_BASE = 1400, /* 1400..1463 bookmark jump ids */
    ID_CTX_SEP = 1490,
    ID_TIMER_SLIDE = 2001
};

typedef enum {
    VIEW_EMPTY = 0,
    VIEW_GRID,
    VIEW_FULL
} ViewMode;

typedef enum {
    LAYOUT_FIT = 0,         /* 화면에 맞춤 */
    LAYOUT_FIT_LARGE,       /* 큰 이미지만 맞춤 */
    LAYOUT_FIT_WIDTH,       /* 너비 맞춤 */
    LAYOUT_FIT_HEIGHT,      /* 높이 맞춤 */
    LAYOUT_ACTUAL,          /* 원본 1:1 (reset pan; may reset on nav unless KEEP) */
    LAYOUT_ACTUAL_KEEP,     /* 원본크기로 계속 보기 */
    LAYOUT_ZOOM_KEEP,       /* 현재 배율 유지 */
    LAYOUT_STRETCH          /* 창에 늘리기 */
} LayoutMode;

typedef enum {
    PAGE_ONE = 1,
    PAGE_TWO = 2,
    PAGE_FOUR = 4
} PageMode;

typedef enum {
    SORT_NAME = 0,
    SORT_DATE = 1,
    SORT_SIZE = 2
} SortMode;

typedef struct {
    COLORREF bg, surface, border, text, muted, accent, accent_dk, grid_bg;
    int dark;
} DabomPalette;

typedef struct {
    wchar_t path[DABOM_MAX_PATH];
    HBITMAP thumb;
    int thumb_w, thumb_h;
    ULONGLONG file_size;
    FILETIME mtime;
} ImageEntry;

typedef struct {
    HWND hwnd;
    UINT dpi; /* window DPI (96 = 100%); Per-Monitor V2 */
    HMENU hmenu, menu_file, menu_ctx, menu_layout, menu_sort, menu_img, menu_bookmarks;
    HBRUSH br_menu;
    HWND status;
    wchar_t status_left[DABOM_MAX_PATH + 64];
    wchar_t status_right[256];
    HFONT font_ui, font_status, font_brand, font_btn;
    HBRUSH br_status;
    IWICImagingFactory *wic;
    DabomPalette palette;
    HICON hicon_big, hicon_sm;
    HBITMAP logo_bmp;
    UINT logo_w, logo_h;
    int logo_is_dark; /* matches last loaded theme variant */

    ImageEntry *files;
    int file_count, file_cap, current;
    int scroll_y, content_h;

    ViewMode mode;
    HBITMAP full_bmp;
    UINT full_w, full_h;
    /* Neighbor bitmaps for multi-page / continuous (lazy). */
    HBITMAP page_bmp[4];
    UINT page_w[4], page_h[4];
    int page_index[4]; /* file index for each slot; -1 unused */

    double zoom;
    int fit_mode; /* 1 when auto-fitting layout */
    LayoutMode layout;
    PageMode page_mode;
    int pan_x, pan_y;
    int dragging, drag_ox, drag_oy;
    int slideshow;

    int fullscreen, continuous, smooth, pixel_info, hdr_preview;
    int pix_x, pix_y, pix_r, pix_g, pix_b, pix_valid;
    WINDOWPLACEMENT fs_placement;
    LONG fs_style;
    HMENU fs_menu;

    /* Zoom navigator (bottom-right overview when zoomed beyond fit). */
    int nav_dismissed;   /* user closed with X until next zoom-out-to-fit */
    int nav_dragging;    /* dragging viewport rect inside navigator */
    RECT nav_rc;         /* last painted navigator bounds (client) */
    RECT nav_view_rc;    /* viewport rectangle inside navigator */
    int view_dx, view_dy, view_dw, view_dh; /* last drawn image dest */
    double view_scale;

    SortMode sort_mode;
    wchar_t bookmarks[DABOM_MAX_BOOKMARKS][DABOM_MAX_PATH];
    int bookmark_count;

    wchar_t last_folder[DABOM_MAX_PATH];
    wchar_t config_path[DABOM_MAX_PATH];
} AppState;

typedef struct {
    const wchar_t *label;
    const wchar_t *shortcut;
} MenuOdItem;

/* image.c */
int dabom_wic_init(AppState *app);
void dabom_wic_shutdown(AppState *app);
HBITMAP dabom_load_bitmap(AppState *app, const wchar_t *path, UINT *out_w, UINT *out_h, UINT max_side);
HBITMAP dabom_load_bitmap_resource(AppState *app, int resid, UINT *out_w, UINT *out_h, UINT max_side);
void dabom_free_bitmap(HBITMAP *pbmp);
void dabom_ensure_logo(AppState *app);
void dabom_draw_logo(AppState *app, HDC hdc, int x, int y, int dest_w, int dest_h);
HBITMAP dabom_rotate_bitmap_90cw(HBITMAP src, UINT *inout_w, UINT *inout_h);
HBITMAP dabom_rotate_bitmap_90ccw(HBITMAP src, UINT *inout_w, UINT *inout_h);
HBITMAP dabom_resize_bitmap(HBITMAP src, UINT src_w, UINT src_h, UINT dst_w, UINT dst_h, int smooth);
HBITMAP dabom_hdr_preview_bitmap(HBITMAP src, UINT w, UINT h);
int dabom_save_bitmap(AppState *app, HBITMAP bmp, UINT w, UINT h,
    const wchar_t *path, const GUID *container_format);
int dabom_get_pixel_rgb(HBITMAP bmp, UINT w, UINT h, int x, int y, int *r, int *g, int *b);
int dabom_copy_bitmap_clipboard(HWND hwnd, HBITMAP bmp, UINT w, UINT h);
HBITMAP dabom_bitmap_from_clipboard(HWND hwnd, UINT *out_w, UINT *out_h);
int dabom_read_camera_info(AppState *app, const wchar_t *path, wchar_t *out, int cch);

/* files.c */
void dabom_clear_files(AppState *app);
int dabom_add_file(AppState *app, const wchar_t *path);
int dabom_scan_folder(AppState *app, const wchar_t *folder);
int dabom_is_image_ext(const wchar_t *path);
void dabom_ensure_thumb(AppState *app, int index);
void dabom_load_current_full(AppState *app);
void dabom_load_page_bitmaps(AppState *app);
void dabom_free_page_bitmaps(AppState *app);
void dabom_sort_files(AppState *app);
void dabom_set_sort_mode(AppState *app, SortMode mode);
int dabom_remove_file_at(AppState *app, int index);

/* config.c */
void dabom_config_path(AppState *app);
void dabom_load_settings(AppState *app);
void dabom_save_settings(AppState *app);
int dabom_bookmark_add(AppState *app, const wchar_t *path);
int dabom_bookmark_find(AppState *app, const wchar_t *path);

/* main.c */
UINT dabom_get_dpi(HWND hwnd);
void dabom_set_dpi_awareness(void);
void dabom_create_fonts(AppState *app);
void dabom_apply_fonts(AppState *app);
int dabom_apps_use_dark_mode(void);
void dabom_theme_apply(AppState *app, int force);
void dabom_set_status(AppState *app, const wchar_t *text);
void dabom_update_chrome(AppState *app);
void dabom_open_folder_dialog(AppState *app);
void dabom_open_files_dialog(AppState *app);
void dabom_goto_index(AppState *app, int index);
void dabom_next(AppState *app, int delta);
void dabom_set_mode(AppState *app, ViewMode mode);
void dabom_toggle_slideshow(AppState *app);
void dabom_zoom_by(AppState *app, double factor);
void dabom_zoom_at(AppState *app, double factor, int mx, int my);
void dabom_fit(AppState *app);
void dabom_actual_size(AppState *app);
void dabom_stretch(AppState *app);
void dabom_set_layout(AppState *app, LayoutMode layout);
void dabom_set_page_mode(AppState *app, PageMode page);
void dabom_menu_colors(const AppState *app,
    COLORREF *bg, COLORREF *text, COLORREF *muted,
    COLORREF *sep, COLORREF *hot_bg, COLORREF *border);
void dabom_measure_od_menu(AppState *app, MEASUREITEMSTRUCT *mis);
void dabom_draw_od_menu(AppState *app, const DRAWITEMSTRUCT *dis);
void dabom_get_content_rect(AppState *app, RECT *rc);

/* ctxmenu.c */
void dabom_ctxmenu_create(AppState *app);
void dabom_ctxmenu_destroy(AppState *app);
void dabom_ctxmenu_theme(AppState *app);
void dabom_ctxmenu_update_checks(AppState *app);
void dabom_ctxmenu_popup(AppState *app, int x, int y);

/* actions.c */
void dabom_toggle_fullscreen(AppState *app);
void dabom_toggle_continuous(AppState *app);
void dabom_toggle_smooth(AppState *app);
void dabom_toggle_pixel_info(AppState *app);
void dabom_toggle_hdr(AppState *app);
void dabom_rotate_current(AppState *app);
void dabom_rotate_current_ccw(AppState *app);
void dabom_copy_current(AppState *app);
void dabom_paste_clipboard(AppState *app);
void dabom_save_as_current(AppState *app);
void dabom_convert_current(AppState *app);
void dabom_resize_dialog(AppState *app);
void dabom_print_current(AppState *app);
void dabom_show_camera_info(AppState *app);
void dabom_show_file_props(AppState *app);
void dabom_delete_current(AppState *app);
void dabom_open_explorer(AppState *app);
void dabom_open_edit_app(AppState *app);
void dabom_copy_to_library(AppState *app);
void dabom_set_wallpaper(AppState *app);
void dabom_bookmark_current(AppState *app);
void dabom_bookmark_jump(AppState *app, int index);
int dabom_map_view_to_image(AppState *app, int mx, int my, int *ix, int *iy);
void dabom_sample_pixel_at(AppState *app, int mx, int my);
int dabom_handle_ctx_command(AppState *app, UINT id);

#endif /* DABOM_H */
