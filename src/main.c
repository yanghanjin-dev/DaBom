#include "dabom.h"
#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
/* Older Win10 builds used attribute 19 for the same immersive dark title bar. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWCP_ROUNDSMALL
#define DWMWCP_ROUNDSMALL 3
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
/* DPI_AWARENESS_CONTEXT values as opaque pointers (avoid SDK typedef friction). */
#define DABOM_DPI_CTX_PER_MONITOR_V2  ((void *)(LONG_PTR)-4)
#define DABOM_DPI_CTX_PER_MONITOR     ((void *)(LONG_PTR)-3)
#define DABOM_DPI_CTX_SYSTEM          ((void *)(LONG_PTR)-2)

static AppState g_app;

static void dabom_update_menubar(AppState *app);
static void layout_statusbar(AppState *app);

/* ---- Per-Monitor DPI V2 ---- */

void dabom_set_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE shcore;

    /* Win10 1703+: Per-Monitor V2 (sharp text + common file dialogs). */
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(void *);
        PFN_SetProcessDpiAwarenessContext set_ctx =
            (PFN_SetProcessDpiAwarenessContext)GetProcAddress(
                user32, "SetProcessDpiAwarenessContext");
        if (set_ctx) {
            if (set_ctx(DABOM_DPI_CTX_PER_MONITOR_V2))
                return;
            if (set_ctx(DABOM_DPI_CTX_PER_MONITOR))
                return;
            if (set_ctx(DABOM_DPI_CTX_SYSTEM))
                return;
        }
    }

    /* Win8.1+: PROCESS_PER_MONITOR_DPI_AWARE */
    shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
        PFN_SetProcessDpiAwareness set_aware =
            (PFN_SetProcessDpiAwareness)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (set_aware) {
            /* PROCESS_PER_MONITOR_DPI_AWARE = 2, PROCESS_SYSTEM_DPI_AWARE = 1 */
            if (SUCCEEDED(set_aware(2)) || SUCCEEDED(set_aware(1))) {
                FreeLibrary(shcore);
                return;
            }
        }
        FreeLibrary(shcore);
    }

    /* Vista+: system DPI aware */
    if (user32) {
        typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
        PFN_SetProcessDPIAware set_legacy =
            (PFN_SetProcessDPIAware)GetProcAddress(user32, "SetProcessDPIAware");
        if (set_legacy)
            set_legacy();
    }
}

UINT dabom_get_dpi(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (hwnd && user32) {
        typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
        PFN_GetDpiForWindow get_win =
            (PFN_GetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        if (get_win) {
            UINT d = get_win(hwnd);
            if (d)
                return d;
        }
    }
    if (user32) {
        typedef UINT (WINAPI *PFN_GetDpiForSystem)(void);
        PFN_GetDpiForSystem get_sys =
            (PFN_GetDpiForSystem)GetProcAddress(user32, "GetDpiForSystem");
        if (get_sys) {
            UINT d = get_sys();
            if (d)
                return d;
        }
    }
    {
        HDC hdc = GetDC(hwnd ? hwnd : NULL);
        UINT d = 96;
        if (hdc) {
            int lx = GetDeviceCaps(hdc, LOGPIXELSX);
            if (lx > 0) d = (UINT)lx;
            ReleaseDC(hwnd ? hwnd : NULL, hdc);
        }
        return d ? d : DABOM_DPI_DEFAULT;
    }
}

static void dabom_free_thumbs(AppState *app)
{
    int i;
    if (!app || !app->files)
        return;
    for (i = 0; i < app->file_count; i++)
        dabom_free_bitmap(&app->files[i].thumb);
}

static void dabom_on_dpi_changed(AppState *app, UINT dpi, const RECT *suggested)
{
    if (!app || !app->hwnd)
        return;
    if (dpi < 72) dpi = DABOM_DPI_DEFAULT;
    app->dpi = dpi;

    if (suggested) {
        SetWindowPos(app->hwnd, NULL,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* Fonts from NONCLIENTMETRICS at the new DPI; regenerate thumbs for sharpness. */
    dabom_create_fonts(app);
    dabom_apply_fonts(app);
    dabom_free_thumbs(app);
    /* Reload high-res logo for the new DPI scale. */
    dabom_free_bitmap(&app->logo_bmp);
    app->logo_w = app->logo_h = 0;
    dabom_ensure_logo(app);

    if (app->status) {
        SendMessageW(app->status, WM_SIZE, 0, 0);
        layout_statusbar(app);
        InvalidateRect(app->status, NULL, TRUE);
    }
    InvalidateRect(app->hwnd, NULL, TRUE);
}

static int menubar_height(const AppState *app)
{
    if (!app || app->fullscreen)
        return 0;
    return DABOM_SCALE(app, DABOM_MENUBAR_H);
}

void dabom_get_content_rect(AppState *app, RECT *rc)
{
    GetClientRect(app->hwnd, rc);
    /* Reserve client menubar strip so images never paint over labels. */
    rc->top += menubar_height(app);
    if (!app->fullscreen && app->status && IsWindowVisible(app->status)) {
        RECT sr;
        GetWindowRect(app->status, &sr);
        rc->bottom -= (sr.bottom - sr.top);
    }
    if (rc->bottom < rc->top)
        rc->bottom = rc->top;
}

/* ---- Windows Apps theme (HKCU Personalize / ShouldAppsUseDarkMode) ---- */

int dabom_apps_use_dark_mode(void)
{
    HMODULE ux;
    HKEY key;
    DWORD value = 1; /* 1 = light (AppsUseLightTheme default) */
    DWORD size = sizeof(value);
    DWORD type = 0;

    /* Undocumented uxtheme export: ShouldAppsUseDarkMode (ordinal 132). */
    ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {
        typedef BOOL (WINAPI *PFN_ShouldAppsUseDarkMode)(void);
        PFN_ShouldAppsUseDarkMode pfn =
            (PFN_ShouldAppsUseDarkMode)GetProcAddress(ux, MAKEINTRESOURCEA(132));
        if (pfn) {
            BOOL dark = pfn();
            FreeLibrary(ux);
            return dark ? 1 : 0;
        }
        FreeLibrary(ux);
    }

    /* Registry: AppsUseLightTheme — 0 = dark, 1 = light. */
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type,
                (LPBYTE)&value, &size) == ERROR_SUCCESS && type == REG_DWORD) {
            RegCloseKey(key);
            return value == 0 ? 1 : 0;
        }
        RegCloseKey(key);
    }
    return 0; /* fallback: previous DaBom charcoal look */
}

static void palette_fill(DabomPalette *p, int dark)
{
    p->dark = dark ? 1 : 0;
    if (dark) {
        /* Cursor Agents chrome: near-black #1e1e1e strip + charcoal content. */
        p->bg        = RGB(0x18, 0x18, 0x18);
        p->surface   = RGB(0x1E, 0x1E, 0x1E); /* menubar + status */
        p->border    = RGB(0x2B, 0x2B, 0x2B);
        p->text      = RGB(0xCC, 0xCC, 0xCC);
        p->muted     = RGB(0x80, 0x80, 0x80);
        p->accent    = RGB(220, 150, 60);
        p->accent_dk = RGB(180, 110, 35);
        p->grid_bg   = RGB(0x14, 0x14, 0x14);
    } else {
        /* Cursor-like light chrome strip; content stays cool gray. */
        p->bg        = RGB(245, 246, 248);
        p->surface   = RGB(0xF3, 0xF3, 0xF3);
        p->border    = RGB(0xE0, 0xE0, 0xE0);
        p->text      = RGB(0x33, 0x33, 0x33);
        p->muted     = RGB(0x6A, 0x6A, 0x6A);
        p->accent    = RGB(180, 110, 30);
        p->accent_dk = RGB(150, 90, 25);
        p->grid_bg   = RGB(232, 235, 239);
    }
}

static void apply_titlebar_dark(HWND hwnd, int dark)
{
    BOOL use_dark = dark ? TRUE : FALSE;
    if (!hwnd)
        return;
    /* Prefer attr 20 (Win10 20H1+ / Win11); fall back to 19 on older builds. */
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
            &use_dark, sizeof(use_dark)))) {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1,
            &use_dark, sizeof(use_dark));
    }
}

void dabom_theme_apply(AppState *app, int force)
{
    int dark = dabom_apps_use_dark_mode();
    /* br_status set only after first apply — safe skip when theme unchanged. */
    if (!force && app->br_status && app->palette.dark == dark)
        return;

    palette_fill(&app->palette, dark);

    if (app->br_status) {
        DeleteObject(app->br_status);
        app->br_status = NULL;
    }
    app->br_status = CreateSolidBrush(app->palette.surface);

    apply_titlebar_dark(app->hwnd, dark);

    /* Refresh themed popup brushes (context TrackPopup; 파일 is custom WS_POPUP). */
    {
        COLORREF bg = dark ? RGB(0x25, 0x25, 0x26) : RGB(0xFF, 0xFF, 0xFF);
        if (app->br_menu) {
            DeleteObject(app->br_menu);
            app->br_menu = NULL;
        }
        app->br_menu = CreateSolidBrush(bg);
        dabom_ctxmenu_theme(app);
    }

    if (app->status) {
        /* Disable visual styles so SB_SETBKCOLOR + owner-draw Apps colors stick. */
        SetWindowTheme(app->status, L"", L"");
        SendMessageW(app->status, SB_SETBKCOLOR, 0, (LPARAM)app->palette.surface);
        InvalidateRect(app->status, NULL, TRUE);
    }

    /* Theme-specific brand logo (light navy / dark light-ink). */
    dabom_free_bitmap(&app->logo_bmp);
    app->logo_w = app->logo_h = 0;
    dabom_ensure_logo(app);

    if (app->hwnd)
        InvalidateRect(app->hwnd, NULL, TRUE);
}

static int status_bar_height(AppState *app)
{
    RECT rc;
    if (!app->status)
        return 0;
    GetWindowRect(app->status, &rc);
    return rc.bottom - rc.top;
}

/* Dock status bar, then split parts: left filename/index, right meta. */
static void layout_statusbar(AppState *app)
{
    RECT rc;
    int parts[2];
    int mid;

    if (!app->status || !app->hwnd)
        return;

    GetClientRect(app->hwnd, &rc);
    SendMessageW(app->status, WM_SIZE, 0, 0);

    mid = rc.right / 2;
    if (mid < 160)
        mid = rc.right > 0 ? rc.right : 0;
    parts[0] = mid;
    parts[1] = -1;
    SendMessageW(app->status, SB_SETPARTS, 2, (LPARAM)parts);
}

static void status_refresh_texts(AppState *app)
{
    if (!app->status)
        return;
    /* SBT_OWNERDRAW: parent paints with Apps palette text/bg. */
    SendMessageW(app->status, SB_SETTEXTW, 0 | SBT_OWNERDRAW,
        (LPARAM)app->status_left);
    SendMessageW(app->status, SB_SETTEXTW, 1 | SBT_OWNERDRAW,
        (LPARAM)app->status_right);
}

void dabom_set_status(AppState *app, const wchar_t *text)
{
    if (text)
        wcsncpy(app->status_left, text, DABOM_MAX_PATH + 63);
    else
        app->status_left[0] = 0;
    app->status_left[DABOM_MAX_PATH + 63] = 0;
    status_refresh_texts(app);
}

static void dabom_set_status_meta(AppState *app, const wchar_t *text)
{
    if (text)
        wcsncpy(app->status_right, text, 255);
    else
        app->status_right[0] = 0;
    app->status_right[255] = 0;
    status_refresh_texts(app);
}

static void draw_status_part(AppState *app, const DRAWITEMSTRUCT *dis)
{
    const wchar_t *text;
    UINT align;
    RECT r;
    HBRUSH br;

    if (!dis || !app)
        return;

    text = (dis->itemID == 0) ? app->status_left : app->status_right;
    align = (dis->itemID == 0)
        ? (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS)
        : (DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    br = app->br_status
        ? app->br_status
        : CreateSolidBrush(app->palette.surface);
    FillRect(dis->hDC, &dis->rcItem, br);
    if (!app->br_status)
        DeleteObject(br);

    r = dis->rcItem;
    InflateRect(&r, -DABOM_SCALE(app, 6), 0);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, app->palette.text);
    if (app->font_status)
        SelectObject(dis->hDC, app->font_status);
    DrawTextW(dis->hDC, text ? text : L"", -1, &r, align);
}

static const wchar_t *format_from_path(const wchar_t *path)
{
    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext || !ext[1])
        return L"Image";
    if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0)
        return L"Jpeg";
    if (_wcsicmp(ext, L".png") == 0)
        return L"Png";
    if (_wcsicmp(ext, L".gif") == 0)
        return L"Gif";
    if (_wcsicmp(ext, L".webp") == 0)
        return L"Webp";
    if (_wcsicmp(ext, L".bmp") == 0)
        return L"Bmp";
    return ext + 1;
}

static void format_file_size(ULONGLONG bytes, wchar_t *out, int cch)
{
    if (bytes < 1024ULL)
        _snwprintf(out, cch, L"%lluB", (unsigned long long)bytes);
    else if (bytes < 1024ULL * 1024ULL)
        _snwprintf(out, cch, L"%.1fKB", (double)bytes / 1024.0);
    else
        _snwprintf(out, cch, L"%.1fMB", (double)bytes / (1024.0 * 1024.0));
    out[cch - 1] = 0;
}

/* Build "YYYY/MM/DD HH:MM:SS WxH(Format) size" for current image; empty if unavailable. */
static void build_image_meta(AppState *app, const wchar_t *path, wchar_t *out, int cch)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    SYSTEMTIME st, local;
    FILETIME local_ft;
    wchar_t size_buf[32];
    const wchar_t *fmt;
    UINT w = app->full_w;
    UINT h = app->full_h;

    out[0] = 0;
    if (!path || !path[0])
        return;

    fmt = format_from_path(path);
    size_buf[0] = 0;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER ul;
        ul.LowPart = fad.nFileSizeLow;
        ul.HighPart = fad.nFileSizeHigh;
        format_file_size(ul.QuadPart, size_buf, 32);
        if (FileTimeToLocalFileTime(&fad.ftLastWriteTime, &local_ft) &&
            FileTimeToSystemTime(&local_ft, &st)) {
            local = st;
            if (w > 0 && h > 0 && size_buf[0]) {
                _snwprintf(out, cch,
                    L"%04u/%02u/%02u %02u:%02u:%02u  %ux%u(%s)  %s",
                    local.wYear, local.wMonth, local.wDay,
                    local.wHour, local.wMinute, local.wSecond,
                    w, h, fmt, size_buf);
            } else if (size_buf[0]) {
                _snwprintf(out, cch,
                    L"%04u/%02u/%02u %02u:%02u:%02u  %s  %s",
                    local.wYear, local.wMonth, local.wDay,
                    local.wHour, local.wMinute, local.wSecond,
                    fmt, size_buf);
            }
            out[cch - 1] = 0;
            return;
        }
    }
    if (w > 0 && h > 0) {
        if (size_buf[0])
            _snwprintf(out, cch, L"%ux%u(%s)  %s", w, h, fmt, size_buf);
        else
            _snwprintf(out, cch, L"%ux%u(%s)", w, h, fmt);
        out[cch - 1] = 0;
    }
}

void dabom_update_chrome(AppState *app)
{
    wchar_t left[DABOM_MAX_PATH + 64];
    wchar_t meta[256];
    wchar_t title[DABOM_MAX_PATH + 80];

    left[0] = 0;
    meta[0] = 0;
    title[0] = 0;

    if (app->mode == VIEW_EMPTY || app->file_count == 0) {
        /* Empty status when no image loaded (reference chrome). */
        SetWindowTextW(app->hwnd, DABOM_TITLE_W);
    } else if (app->mode == VIEW_FULL && app->current >= 0 && app->current < app->file_count) {
        const wchar_t *path = app->files[app->current].path;
        const wchar_t *name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        _snwprintf(left, DABOM_MAX_PATH + 64, L"%s  (%d / %d)%s%s",
            name, app->current + 1, app->file_count,
            app->slideshow ? L"  · 슬라이드쇼" : L"",
            app->hdr_preview ? L"  · HDR" : L"");
        left[DABOM_MAX_PATH + 63] = 0;
        build_image_meta(app, path, meta, 256);
        if (!app->pixel_info && app->zoom > 0) {
            wchar_t zbuf[32];
            _snwprintf(zbuf, 32, L"  %d%%", (int)(app->zoom * 100.0 + 0.5));
            zbuf[31] = 0;
            if (wcslen(meta) + wcslen(zbuf) < 255)
                wcscat(meta, zbuf);
        }
        _snwprintf(title, DABOM_MAX_PATH + 80, L"DaBom - %s (%d / %d)",
            name, app->current + 1, app->file_count);
        title[DABOM_MAX_PATH + 79] = 0;
        SetWindowTextW(app->hwnd, title);
    } else {
        _snwprintf(left, DABOM_MAX_PATH + 64, L"이미지 %d장", app->file_count);
        left[DABOM_MAX_PATH + 63] = 0;
        SetWindowTextW(app->hwnd, DABOM_TITLE_W);
    }

    dabom_set_status(app, left);
    dabom_set_status_meta(app, meta);
    dabom_update_menubar(app);
}

void dabom_set_mode(AppState *app, ViewMode mode)
{
    app->mode = mode;
    if (mode != VIEW_FULL && app->slideshow) {
        KillTimer(app->hwnd, ID_TIMER_SLIDE);
        app->slideshow = 0;
    }
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_goto_index(AppState *app, int index)
{
    if (app->file_count <= 0)
        return;
    if (index < 0)
        index = app->file_count - 1;
    if (index >= app->file_count)
        index = 0;
    app->current = index;
    dabom_load_current_full(app);
    dabom_set_mode(app, VIEW_FULL);
}

void dabom_next(AppState *app, int delta)
{
    if (app->file_count <= 0)
        return;
    dabom_goto_index(app, app->current + delta);
}

void dabom_set_layout(AppState *app, LayoutMode layout)
{
    app->layout = layout;
    switch (layout) {
    case LAYOUT_ACTUAL:
    case LAYOUT_ACTUAL_KEEP:
        app->fit_mode = 0;
        app->zoom = 1.0;
        break;
    case LAYOUT_ZOOM_KEEP:
        app->fit_mode = 0;
        if (app->zoom <= 0) app->zoom = 1.0;
        break;
    case LAYOUT_STRETCH:
        app->fit_mode = 1;
        break;
    default:
        app->fit_mode = 1;
        break;
    }
    app->pan_x = app->pan_y = 0;
    if (app->mode != VIEW_FULL && (app->full_bmp || app->file_count > 0)) {
        if (!app->full_bmp) dabom_load_current_full(app);
        dabom_set_mode(app, VIEW_FULL);
    }
    dabom_save_settings(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_set_page_mode(AppState *app, PageMode page)
{
    app->page_mode = page;
    if (app->file_count > 0) {
        if (app->mode != VIEW_FULL)
            dabom_set_mode(app, VIEW_FULL);
        dabom_load_page_bitmaps(app);
    }
    dabom_save_settings(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_fit(AppState *app)
{
    dabom_set_layout(app, LAYOUT_FIT);
}

void dabom_actual_size(AppState *app)
{
    if (app->mode != VIEW_FULL || !app->full_bmp)
        return;
    dabom_set_layout(app, LAYOUT_ACTUAL);
}

void dabom_stretch(AppState *app)
{
    if (!app->full_bmp)
        return;
    dabom_set_layout(app, LAYOUT_STRETCH);
}

void dabom_zoom_by(AppState *app, double factor)
{
    RECT rc;
    int cx, cy;
    if (app->mode != VIEW_FULL || !app->full_bmp)
        return;
    dabom_get_content_rect(app, &rc);
    cx = (rc.left + rc.right) / 2;
    cy = (rc.top + rc.bottom) / 2;
    dabom_zoom_at(app, factor, cx, cy);
}

void dabom_zoom_at(AppState *app, double factor, int mx, int my)
{
    RECT rc;
    double zw, zh, fit, old_z, new_z;
    int cw, ch, old_dw, old_dh, old_dx, old_dy;
    int new_dw, new_dh;
    double img_x, img_y;

    if (app->mode != VIEW_FULL || !app->full_bmp)
        return;

    dabom_get_content_rect(app, &rc);
    cw = rc.right - rc.left;
    ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0)
        return;

    zw = (double)cw / (double)app->full_w;
    zh = (double)ch / (double)app->full_h;
    fit = zw < zh ? zw : zh;
    if (fit <= 0) fit = 1.0;

    if (app->fit_mode || app->layout == LAYOUT_FIT || app->layout == LAYOUT_FIT_LARGE ||
        app->layout == LAYOUT_FIT_WIDTH || app->layout == LAYOUT_FIT_HEIGHT ||
        app->layout == LAYOUT_STRETCH) {
        if (app->layout == LAYOUT_FIT_WIDTH) old_z = zw;
        else if (app->layout == LAYOUT_FIT_HEIGHT) old_z = zh;
        else if (app->layout == LAYOUT_FIT_LARGE) {
            old_z = fit;
            if (old_z > 1.0) old_z = 1.0;
        } else if (app->layout == LAYOUT_STRETCH) {
            old_z = fit; /* approximate for zoom handoff */
        } else
            old_z = fit;
        app->zoom = old_z;
        app->fit_mode = 0;
        if (app->layout != LAYOUT_ZOOM_KEEP && app->layout != LAYOUT_ACTUAL_KEEP)
            app->layout = LAYOUT_ZOOM_KEEP;
    } else {
        old_z = app->zoom;
    }

    old_dw = (int)(app->full_w * old_z + 0.5);
    old_dh = (int)(app->full_h * old_z + 0.5);
    old_dx = rc.left + (cw - old_dw) / 2 + app->pan_x;
    old_dy = rc.top + (ch - old_dh) / 2 + app->pan_y;

    /* Image pixel under cursor before zoom. */
    if (old_z > 0) {
        img_x = (double)(mx - old_dx) / old_z;
        img_y = (double)(my - old_dy) / old_z;
    } else {
        img_x = app->full_w * 0.5;
        img_y = app->full_h * 0.5;
    }

    new_z = old_z * factor;
    if (new_z < DABOM_ZOOM_MIN) new_z = DABOM_ZOOM_MIN;
    if (new_z > DABOM_ZOOM_MAX) new_z = DABOM_ZOOM_MAX;
    app->zoom = new_z;

    new_dw = (int)(app->full_w * new_z + 0.5);
    new_dh = (int)(app->full_h * new_z + 0.5);
    /* Keep img_x,img_y under cursor. */
    app->pan_x = mx - rc.left - (cw - new_dw) / 2 - (int)(img_x * new_z + 0.5);
    app->pan_y = my - rc.top - (ch - new_dh) / 2 - (int)(img_y * new_z + 0.5);

    app->nav_dismissed = 0;
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_toggle_slideshow(AppState *app)
{
    if (app->file_count <= 0)
        return;
    if (app->mode != VIEW_FULL)
        dabom_goto_index(app, app->current);
    app->slideshow = !app->slideshow;
    if (app->slideshow)
        SetTimer(app->hwnd, ID_TIMER_SLIDE, DABOM_SLIDE_MS, NULL);
    else
        KillTimer(app->hwnd, ID_TIMER_SLIDE);
    dabom_update_chrome(app);
}

static HRESULT open_dialog(AppState *app, int folders)
{
    IFileOpenDialog *dlg = NULL;
    HRESULT hr = CoCreateInstance(
        &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        &IID_IFileOpenDialog, (void **)&dlg);
    if (FAILED(hr) || !dlg)
        return hr;

    DWORD opts = 0;
    IFileOpenDialog_GetOptions(dlg, &opts);
    if (folders) {
        opts |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM;
    } else {
        opts |= FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST;
        COMDLG_FILTERSPEC filters[] = {
            { L"이미지", L"*.jpg;*.jpeg;*.png;*.gif;*.webp;*.bmp" },
            { L"모든 파일", L"*.*" }
        };
        IFileOpenDialog_SetFileTypes(dlg, 2, filters);
    }
    IFileOpenDialog_SetOptions(dlg, opts);

    if (app->last_folder[0]) {
        IShellItem *folder = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(app->last_folder, NULL, &IID_IShellItem, (void **)&folder)) && folder) {
            IFileOpenDialog_SetFolder(dlg, folder);
            IShellItem_Release(folder);
        }
    }

    hr = IFileOpenDialog_Show(dlg, app->hwnd);
    if (SUCCEEDED(hr)) {
        if (folders) {
            IShellItem *item = NULL;
            if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item)) && item) {
                PWSTR path = NULL;
                if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                    dabom_scan_folder(app, path);
                    CoTaskMemFree(path);
                    if (app->file_count > 0) {
                        app->current = 0;
                        dabom_set_mode(app, VIEW_GRID);
                    } else {
                        MessageBoxW(app->hwnd, L"이 폴더에서 지원 이미지를 찾지 못했습니다.", L"DaBom", MB_OK | MB_ICONINFORMATION);
                        dabom_set_mode(app, VIEW_EMPTY);
                    }
                }
                IShellItem_Release(item);
            }
        } else {
            IShellItemArray *arr = NULL;
            if (SUCCEEDED(IFileOpenDialog_GetResults(dlg, &arr)) && arr) {
                DWORD count = 0, i;
                IShellItemArray_GetCount(arr, &count);
                dabom_clear_files(app);
                for (i = 0; i < count; i++) {
                    IShellItem *item = NULL;
                    if (SUCCEEDED(IShellItemArray_GetItemAt(arr, i, &item)) && item) {
                        PWSTR path = NULL;
                        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                            dabom_add_file(app, path);
                            {
                                wchar_t *slash = wcsrchr(path, L'\\');
                                if (slash) {
                                    wchar_t dir[DABOM_MAX_PATH];
                                    size_t n = (size_t)(slash - path);
                                    if (n >= DABOM_MAX_PATH) n = DABOM_MAX_PATH - 1;
                                    wcsncpy(dir, path, n);
                                    dir[n] = 0;
                                    wcsncpy(app->last_folder, dir, DABOM_MAX_PATH - 1);
                                    app->last_folder[DABOM_MAX_PATH - 1] = 0;
                                }
                            }
                            CoTaskMemFree(path);
                        }
                        IShellItem_Release(item);
                    }
                }
                IShellItemArray_Release(arr);
                if (app->file_count > 1) {
                    int a, b;
                    for (a = 0; a < app->file_count - 1; a++) {
                        for (b = a + 1; b < app->file_count; b++) {
                            if (_wcsicmp(app->files[a].path, app->files[b].path) > 0) {
                                ImageEntry tmp = app->files[a];
                                app->files[a] = app->files[b];
                                app->files[b] = tmp;
                            }
                        }
                    }
                }
                dabom_save_settings(app);
                if (app->file_count > 0) {
                    app->current = 0;
                    dabom_set_mode(app, VIEW_GRID);
                }
            }
        }
    }
    IFileOpenDialog_Release(dlg);
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
    return hr;
}

void dabom_open_folder_dialog(AppState *app) { open_dialog(app, 1); }
void dabom_open_files_dialog(AppState *app) { open_dialog(app, 0); }

/* dabom_get_content_rect defined near top */

static void paint_empty(AppState *app, HDC hdc, const RECT *rc)
{
    RECT r = *rc;
    const wchar_t *ver = DABOM_TITLE_W; /* e.g. DaBom 0.0.8 */
    const wchar_t *sub = L"사진 폴더를 열어보세요";
    const wchar_t *hint = L"폴더 열기 또는 파일 열기로 이미지를 불러옵니다";
    SIZE sz;
    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;
    int logo_w = DABOM_SCALE(app, 320);
    int logo_h;
    int logo_x, logo_y;
    int gap = DABOM_SCALE(app, 18);
    int y_cursor;

    {
        HBRUSH br = CreateSolidBrush(app->palette.bg);
        FillRect(hdc, &r, br);
        DeleteObject(br);
    }

    dabom_ensure_logo(app);
    if (app->logo_bmp && app->logo_w > 0 && app->logo_h > 0) {
        logo_h = (int)((LONGLONG)logo_w * app->logo_h / app->logo_w);
        if (logo_h < 1) logo_h = 1;
        logo_x = cx - logo_w / 2;
        logo_y = cy - logo_h / 2 - DABOM_SCALE(app, 36);
        dabom_draw_logo(app, hdc, logo_x, logo_y, logo_w, logo_h);
        y_cursor = logo_y + logo_h + gap;
    } else {
        /* Fallback if resource logo missing. */
        const wchar_t *brand = L"다봄 · DaBom";
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, app->font_brand);
        SetTextColor(hdc, app->palette.accent);
        GetTextExtentPoint32W(hdc, brand, (int)wcslen(brand), &sz);
        TextOutW(hdc, cx - sz.cx / 2, cy - DABOM_SCALE(app, 80), brand, (int)wcslen(brand));
        y_cursor = cy - DABOM_SCALE(app, 18);
    }

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, app->font_ui);
    SetTextColor(hdc, app->palette.muted);
    GetTextExtentPoint32W(hdc, ver, (int)wcslen(ver), &sz);
    TextOutW(hdc, cx - sz.cx / 2, y_cursor, ver, (int)wcslen(ver));
    y_cursor += sz.cy + DABOM_SCALE(app, 14);

    SetTextColor(hdc, app->palette.text);
    GetTextExtentPoint32W(hdc, sub, (int)wcslen(sub), &sz);
    TextOutW(hdc, cx - sz.cx / 2, y_cursor, sub, (int)wcslen(sub));
    y_cursor += sz.cy + DABOM_SCALE(app, 10);

    SetTextColor(hdc, app->palette.muted);
    GetTextExtentPoint32W(hdc, hint, (int)wcslen(hint), &sz);
    TextOutW(hdc, cx - sz.cx / 2, y_cursor, hint, (int)wcslen(hint));
}

static void paint_grid(AppState *app, HDC hdc, const RECT *rc)
{
    int thumb = DABOM_SCALE(app, DABOM_THUMB);
    int pad = DABOM_SCALE(app, DABOM_THUMB_PAD);
    int label_h = DABOM_SCALE(app, 22);
    int cell = thumb + pad * 2;
    int cols, rows, i;
    int width = rc->right - rc->left;
    int height = rc->bottom - rc->top;
    HBRUSH bg = CreateSolidBrush(app->palette.grid_bg);
    HBRUSH cellbr = CreateSolidBrush(app->palette.surface);
    HPEN pen = CreatePen(PS_SOLID, 1, app->palette.border);

    FillRect(hdc, rc, bg);

    if (width < cell)
        cols = 1;
    else
        cols = width / cell;
    if (cols < 1) cols = 1;
    rows = (app->file_count + cols - 1) / cols;
    app->content_h = rows * cell;

    SelectObject(hdc, cellbr);
    SelectObject(hdc, pen);
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, app->font_ui);
    SetTextColor(hdc, app->palette.muted);

    for (i = 0; i < app->file_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = rc->left + col * cell + pad;
        int y = rc->top + row * cell + pad - app->scroll_y;
        RECT cellrc;
        const wchar_t *name;

        if (y + thumb < rc->top || y > rc->bottom)
            continue;

        dabom_ensure_thumb(app, i);

        cellrc.left = x - DABOM_SCALE(app, 4);
        cellrc.top = y - DABOM_SCALE(app, 4);
        cellrc.right = x + thumb + DABOM_SCALE(app, 4);
        cellrc.bottom = y + thumb + label_h;
        RoundRect(hdc, cellrc.left, cellrc.top, cellrc.right, cellrc.bottom,
            DABOM_SCALE(app, 6), DABOM_SCALE(app, 6));

        if (app->files[i].thumb) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP old = (HBITMAP)SelectObject(mem, app->files[i].thumb);
            int tw = app->files[i].thumb_w;
            int th = app->files[i].thumb_h;
            int dx = x + (thumb - tw) / 2;
            int dy = y + (thumb - th) / 2;
            SetStretchBltMode(hdc, HALFTONE);
            /* Thumbs are device-pixel bitmaps; 1:1 BitBlt (no logical stretch). */
            BitBlt(hdc, dx, dy, tw, th, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }

        name = wcsrchr(app->files[i].path, L'\\');
        name = name ? name + 1 : app->files[i].path;
        {
            RECT tr = { x, y + thumb + DABOM_SCALE(app, 2),
                x + thumb, y + thumb + DABOM_SCALE(app, 20) };
            DrawTextW(hdc, name, -1, &tr, DT_CENTER | DT_END_ELLIPSIS | DT_SINGLELINE);
        }
    }

    DeleteObject(pen);
    DeleteObject(cellbr);
    DeleteObject(bg);

    /* scrollbar range via scroll_y clamp on wheel */
    {
        int max_scroll = app->content_h - height;
        if (max_scroll < 0) max_scroll = 0;
        if (app->scroll_y > max_scroll) app->scroll_y = max_scroll;
        if (app->scroll_y < 0) app->scroll_y = 0;
    }
}

static double compute_layout_scale(AppState *app, int cw, int ch, UINT iw, UINT ih)
{
    double zw = (double)cw / (double)iw;
    double zh = (double)ch / (double)ih;
    double fit = zw < zh ? zw : zh;
    if (fit <= 0) fit = 1.0;
    switch (app->layout) {
    case LAYOUT_FIT_WIDTH: return zw > 0 ? zw : 1.0;
    case LAYOUT_FIT_HEIGHT: return zh > 0 ? zh : 1.0;
    case LAYOUT_FIT_LARGE:
        return fit > 1.0 ? 1.0 : fit;
    case LAYOUT_STRETCH:
        return -1.0; /* special: fill */
    case LAYOUT_ACTUAL:
    case LAYOUT_ACTUAL_KEEP:
        return 1.0;
    case LAYOUT_ZOOM_KEEP:
        return app->zoom > 0 ? app->zoom : 1.0;
    case LAYOUT_FIT:
    default:
        return fit;
    }
}

static void blit_image(AppState *app, HDC hdc, HBITMAP bmp, UINT iw, UINT ih,
    int dx, int dy, int dw, int dh)
{
    HBITMAP draw = bmp;
    HBITMAP hdr = NULL;
    HDC mem;
    HBITMAP old;
    if (!bmp || iw == 0 || ih == 0 || dw <= 0 || dh <= 0)
        return;
    if (app->hdr_preview) {
        hdr = dabom_hdr_preview_bitmap(bmp, iw, ih);
        if (hdr) draw = hdr;
    }
    mem = CreateCompatibleDC(hdc);
    old = (HBITMAP)SelectObject(mem, draw);
    SetStretchBltMode(hdc, app->smooth ? HALFTONE : COLORONCOLOR);
    if (app->smooth)
        SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchBlt(hdc, dx, dy, dw, dh, mem, 0, 0, (int)iw, (int)ih, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    if (hdr) DeleteObject(hdr);
}

static int nav_should_show(AppState *app, int cw, int ch, int dw, int dh)
{
    if (app->nav_dismissed || app->mode != VIEW_FULL || !app->full_bmp)
        return 0;
    if (app->continuous || app->page_mode != PAGE_ONE)
        return 0;
    if (app->layout == LAYOUT_STRETCH)
        return 0;
    /* Show when image is larger than viewport (not fully visible). */
    return (dw > cw + 1) || (dh > ch + 1);
}

static void paint_navigator(AppState *app, HDC hdc, const RECT *rc, int dx, int dy, int dw, int dh)
{
    const int nav_w = DABOM_SCALE(app, 160);
    const int nav_h = DABOM_SCALE(app, 120);
    const int pad = DABOM_SCALE(app, 10);
    const int title_h = DABOM_SCALE(app, 18);
    const int inset = DABOM_SCALE(app, 6);
    RECT box, xbtn, thumb;
    int cw = rc->right - rc->left;
    int ch = rc->bottom - rc->top;
    double ts, vx, vy, vw, vh;
    int tw, th, tdx, tdy;
    HBRUSH br;
    HPEN pen, oldp;
    HDC mem;
    HBITMAP old;

    box.right = rc->right - pad;
    box.bottom = rc->bottom - pad;
    box.left = box.right - nav_w;
    box.top = box.bottom - nav_h;
    app->nav_rc = box;

    br = CreateSolidBrush(app->palette.dark ? RGB(40, 44, 48) : RGB(250, 250, 252));
    FillRect(hdc, &box, br);
    DeleteObject(br);
    pen = CreatePen(PS_SOLID, 1, app->palette.border);
    oldp = (HPEN)SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, box.left, box.top, box.right, box.bottom);

    /* Title + close */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, app->palette.text);
    if (app->font_ui) SelectObject(hdc, app->font_ui);
    TextOutW(hdc, box.left + inset, box.top + DABOM_SCALE(app, 2), L"탐색", 2);
    xbtn.left = box.right - DABOM_SCALE(app, 18);
    xbtn.top = box.top + DABOM_SCALE(app, 2);
    xbtn.right = box.right - DABOM_SCALE(app, 4);
    xbtn.bottom = box.top + title_h - DABOM_SCALE(app, 2);
    DrawTextW(hdc, L"X", 1, &xbtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    thumb.left = box.left + inset;
    thumb.top = box.top + title_h;
    thumb.right = box.right - inset;
    thumb.bottom = box.bottom - inset;
    tw = thumb.right - thumb.left;
    th = thumb.bottom - thumb.top;
    ts = (double)tw / (double)app->full_w;
    if ((double)app->full_h * ts > (double)th)
        ts = (double)th / (double)app->full_h;
    tw = (int)(app->full_w * ts + 0.5);
    th = (int)(app->full_h * ts + 0.5);
    tdx = thumb.left + ((thumb.right - thumb.left) - tw) / 2;
    tdy = thumb.top + ((thumb.bottom - thumb.top) - th) / 2;

    mem = CreateCompatibleDC(hdc);
    old = (HBITMAP)SelectObject(mem, app->full_bmp);
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, tdx, tdy, tw, th, mem, 0, 0, (int)app->full_w, (int)app->full_h, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);

    /* Viewport rect in navigator coords */
    if (dw > 0 && dh > 0 && ts > 0) {
        vx = (double)(rc->left - dx) * ts;
        vy = (double)(rc->top - dy) * ts;
        vw = (double)cw * ts;
        vh = (double)ch * ts;
        if (vx < 0) { vw += vx; vx = 0; }
        if (vy < 0) { vh += vy; vy = 0; }
        if (vx + vw > tw) vw = tw - vx;
        if (vy + vh > th) vh = th - vy;
        app->nav_view_rc.left = tdx + (int)vx;
        app->nav_view_rc.top = tdy + (int)vy;
        app->nav_view_rc.right = app->nav_view_rc.left + (int)(vw < 2 ? 2 : vw);
        app->nav_view_rc.bottom = app->nav_view_rc.top + (int)(vh < 2 ? 2 : vh);
        SelectObject(hdc, CreatePen(PS_SOLID, 2, app->palette.accent));
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, app->nav_view_rc.left, app->nav_view_rc.top,
            app->nav_view_rc.right, app->nav_view_rc.bottom);
    }

    SelectObject(hdc, oldp);
    DeleteObject(pen);
    (void)ch;
}

static void paint_one_slot(AppState *app, HDC hdc, const RECT *cell,
    HBITMAP bmp, UINT iw, UINT ih, int apply_pan)
{
    int cw = cell->right - cell->left;
    int ch = cell->bottom - cell->top;
    double scale;
    int dw, dh, dx, dy;

    if (!bmp || iw == 0 || ih == 0 || cw <= 0 || ch <= 0)
        return;
    scale = compute_layout_scale(app, cw, ch, iw, ih);
    if (scale < 0) {
        /* stretch */
        blit_image(app, hdc, bmp, iw, ih, cell->left, cell->top, cw, ch);
        if (apply_pan) {
            app->view_dx = cell->left; app->view_dy = cell->top;
            app->view_dw = cw; app->view_dh = ch;
            app->view_scale = 1.0;
            app->zoom = 1.0;
        }
        return;
    }
    if (apply_pan && !app->fit_mode &&
        (app->layout == LAYOUT_ZOOM_KEEP || app->layout == LAYOUT_ACTUAL ||
         app->layout == LAYOUT_ACTUAL_KEEP)) {
        scale = app->zoom > 0 ? app->zoom : scale;
    } else if (app->fit_mode || app->layout == LAYOUT_FIT || app->layout == LAYOUT_FIT_LARGE ||
               app->layout == LAYOUT_FIT_WIDTH || app->layout == LAYOUT_FIT_HEIGHT) {
        if (apply_pan) {
            app->zoom = scale;
            app->pan_x = app->pan_y = 0;
        }
    }

    dw = (int)(iw * scale + 0.5);
    dh = (int)(ih * scale + 0.5);
    dx = cell->left + (cw - dw) / 2 + (apply_pan ? app->pan_x : 0);
    dy = cell->top + (ch - dh) / 2 + (apply_pan ? app->pan_y : 0);
    blit_image(app, hdc, bmp, iw, ih, dx, dy, dw, dh);
    if (apply_pan) {
        app->view_dx = dx; app->view_dy = dy;
        app->view_dw = dw; app->view_dh = dh;
        app->view_scale = scale;
    }
}

static void paint_full(AppState *app, HDC hdc, const RECT *rc)
{
    HBRUSH bg = CreateSolidBrush(app->palette.bg);
    int cw = rc->right - rc->left;
    int ch = rc->bottom - rc->top;
    int i;

    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    if ((!app->full_bmp || app->full_w == 0 || app->full_h == 0) && app->file_count <= 0) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, app->palette.muted);
        SelectObject(hdc, app->font_ui);
        {
            const wchar_t *err = L"이미지를 불러올 수 없습니다.";
            TextOutW(hdc, rc->left + DABOM_SCALE(app, 24),
                rc->top + DABOM_SCALE(app, 24), err, (int)wcslen(err));
        }
        return;
    }

    if (app->continuous) {
        int y = rc->top - app->scroll_y;
        int max_i = app->file_count;
        int drawn = 0;
        for (i = app->current; i < max_i && y < rc->bottom + 400; i++) {
            HBITMAP bmp = NULL;
            UINT iw = 0, ih = 0;
            RECT cell;
            int slot_h;
            if (i == app->current && app->full_bmp) {
                bmp = app->full_bmp; iw = app->full_w; ih = app->full_h;
            } else {
                int s = i - app->current;
                if (s >= 0 && s < 4 && app->page_index[s] == i && app->page_bmp[s]) {
                    bmp = app->page_bmp[s]; iw = app->page_w[s]; ih = app->page_h[s];
                } else {
                    dabom_ensure_thumb(app, i);
                    continue;
                }
            }
            if (!bmp) continue;
            {
                double sc = (double)cw / (double)iw;
                if (sc <= 0) sc = 1.0;
                slot_h = (int)(ih * sc + 0.5) + 12;
            }
            cell.left = rc->left;
            cell.right = rc->right;
            cell.top = y;
            cell.bottom = y + slot_h - 12;
            paint_one_slot(app, hdc, &cell, bmp, iw, ih, i == app->current);
            y += slot_h;
            drawn++;
            if (drawn >= 4) break;
        }
        app->content_h = (y + app->scroll_y) - rc->top + 40;
        return;
    }

    if (app->page_mode == PAGE_TWO) {
        RECT left = *rc, right = *rc;
        left.right = rc->left + cw / 2 - 2;
        right.left = rc->left + cw / 2 + 2;
        paint_one_slot(app, hdc, &left, app->full_bmp, app->full_w, app->full_h, 1);
        if (app->page_index[1] >= 0 && app->page_bmp[1])
            paint_one_slot(app, hdc, &right, app->page_bmp[1], app->page_w[1], app->page_h[1], 0);
        return;
    }

    if (app->page_mode == PAGE_FOUR) {
        int mw = cw / 2 - 2, mh = ch / 2 - 2;
        RECT cells[4];
        cells[0].left = rc->left; cells[0].top = rc->top;
        cells[0].right = rc->left + mw; cells[0].bottom = rc->top + mh;
        cells[1].left = rc->left + mw + 4; cells[1].top = rc->top;
        cells[1].right = rc->right; cells[1].bottom = rc->top + mh;
        cells[2].left = rc->left; cells[2].top = rc->top + mh + 4;
        cells[2].right = rc->left + mw; cells[2].bottom = rc->bottom;
        cells[3].left = rc->left + mw + 4; cells[3].top = rc->top + mh + 4;
        cells[3].right = rc->right; cells[3].bottom = rc->bottom;
        for (i = 0; i < 4; i++) {
            HBITMAP bmp = NULL;
            UINT iw = 0, ih = 0;
            if (i == 0 && app->full_bmp) {
                bmp = app->full_bmp; iw = app->full_w; ih = app->full_h;
            } else if (app->page_index[i] >= 0 && app->page_bmp[i]) {
                bmp = app->page_bmp[i]; iw = app->page_w[i]; ih = app->page_h[i];
            }
            if (bmp)
                paint_one_slot(app, hdc, &cells[i], bmp, iw, ih, i == 0);
        }
        return;
    }

    /* Single page */
    if (!app->full_bmp) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, app->palette.muted);
        SelectObject(hdc, app->font_ui);
        {
            const wchar_t *err = L"이미지를 불러올 수 없습니다.";
            TextOutW(hdc, rc->left + DABOM_SCALE(app, 24),
                rc->top + DABOM_SCALE(app, 24), err, (int)wcslen(err));
        }
        return;
    }
    paint_one_slot(app, hdc, rc, app->full_bmp, app->full_w, app->full_h, 1);

    if (nav_should_show(app, cw, ch, app->view_dw, app->view_dh))
        paint_navigator(app, hdc, rc, app->view_dx, app->view_dy, app->view_dw, app->view_dh);
    else
        ZeroMemory(&app->nav_rc, sizeof(app->nav_rc));

    if (app->pixel_info && app->pix_valid) {
        wchar_t tip[64];
        RECT tr;
        _snwprintf(tip, 64, L"(%d,%d) RGB(%d,%d,%d)",
            app->pix_x, app->pix_y, app->pix_r, app->pix_g, app->pix_b);
        tip[63] = 0;
        tr.left = rc->left + DABOM_SCALE(app, 12);
        tr.top = rc->top + DABOM_SCALE(app, 12);
        tr.right = tr.left + DABOM_SCALE(app, 280);
        tr.bottom = tr.top + DABOM_SCALE(app, 28);
        {
            HBRUSH b = CreateSolidBrush(app->palette.surface);
            FillRect(hdc, &tr, b);
            DeleteObject(b);
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, app->palette.text);
        if (app->font_ui) SelectObject(hdc, app->font_ui);
        DrawTextW(hdc, tip, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void paint_menubar(AppState *app, HDC hdc, int w); /* defined below */

static void paint_all(AppState *app)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(app->hwnd, &ps);
    RECT rc, content;
    HDC mem;
    HBITMAP dib, old;
    int w, h, mh;
    int saved;

    GetClientRect(app->hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        EndPaint(app->hwnd, &ps);
        return;
    }

    mem = CreateCompatibleDC(hdc);
    dib = CreateCompatibleBitmap(hdc, w, h);
    old = (HBITMAP)SelectObject(mem, dib);

    dabom_get_content_rect(app, &content);
    mh = menubar_height(app);

    /* Paint image/content only inside content rect (clip = no cover menubar). */
    {
        HBRUSH br = CreateSolidBrush(app->palette.bg);
        FillRect(mem, &content, br);
        DeleteObject(br);
    }

    saved = SaveDC(mem);
    IntersectClipRect(mem, content.left, content.top, content.right, content.bottom);
    if (app->mode == VIEW_EMPTY)
        paint_empty(app, mem, &content);
    else if (app->mode == VIEW_GRID)
        paint_grid(app, mem, &content);
    else
        paint_full(app, mem, &content);
    RestoreDC(mem, saved);

    /* Menubar last so it always sits above any spilled content pixels. */
    if (mh > 0)
        paint_menubar(app, mem, w);

    if (mh > 0)
        BitBlt(hdc, 0, 0, w, mh, mem, 0, 0, SRCCOPY);
    BitBlt(hdc, content.left, content.top,
        content.right - content.left, content.bottom - content.top,
        mem, content.left, content.top, SRCCOPY);
    (void)h;
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    EndPaint(app->hwnd, &ps);
}

static int hit_grid_index(AppState *app, int mx, int my)
{
    RECT rc;
    int cell = DABOM_SCALE(app, DABOM_THUMB) + DABOM_SCALE(app, DABOM_THUMB_PAD) * 2;
    int cols, col, row, idx;
    dabom_get_content_rect(app, &rc);
    if (my < rc.top || my > rc.bottom)
        return -1;
    {
        int width = rc.right - rc.left;
        cols = width / cell;
        if (cols < 1) cols = 1;
    }
    col = (mx - rc.left) / cell;
    row = (my - rc.top + app->scroll_y) / cell;
    if (col < 0 || col >= cols)
        return -1;
    idx = row * cols + col;
    if (idx < 0 || idx >= app->file_count)
        return -1;
    return idx;
}

/* ---- Custom client menubar (Cursor-like flat strip; themed 파일 popup) ---- */

enum {
    FILE_MENU_SEP_ID = 1 /* non-command id for owner-draw separator row */
};

/* Lifetime statics — itemData pointers stay valid while menubar exists. */
static const MenuOdItem g_file_items[] = {
    { L"폴더 열기",     NULL,  0 },
    { L"파일 열기",     NULL,  0 },
    { L"파일 정보",     L"TAB", 0 },
    { NULL,             NULL,  0 }, /* separator */
    { L"프로그램 정보", L"F1", 0 },
    { L"종료",          L"X",  0 },
};
static const UINT g_file_ids[] = {
    ID_MENU_OPEN_FOLDER,
    ID_MENU_OPEN_FILES,
    ID_MENU_FILE_INFO,
    FILE_MENU_SEP_ID,
    ID_MENU_ABOUT,
    ID_MENU_EXIT,
};

typedef struct {
    int id;               /* 0 = separator */
    const wchar_t *label; /* NULL for pure sep */
    int file_chevron;
} MbDef;

void dabom_menu_colors(const AppState *app,
    COLORREF *bg, COLORREF *text, COLORREF *muted,
    COLORREF *sep, COLORREF *hot_bg, COLORREF *border)
{
    if (app->palette.dark) {
        /* Cursor Agents File popup: #252526 / #CCCCCC / hover #2A2D2E. */
        *bg     = RGB(0x25, 0x25, 0x26);
        *text   = RGB(0xCC, 0xCC, 0xCC);
        *muted  = RGB(0x89, 0x89, 0x89);
        *sep    = RGB(0x3C, 0x3C, 0x3C);
        *hot_bg = RGB(0x2A, 0x2D, 0x2E);
        *border = RGB(0x45, 0x45, 0x45);
    } else {
        /* Cursor-like light popup (white, not classic gray). */
        *bg     = RGB(0xFF, 0xFF, 0xFF);
        *text   = RGB(0x33, 0x33, 0x33);
        *muted  = RGB(0x6A, 0x6A, 0x6A);
        *sep    = RGB(0xE5, 0xE5, 0xE5);
        *hot_bg = RGB(0xF0, 0xF0, 0xF0);
        *border = RGB(0xD0, 0xD0, 0xD0);
    }
}

/* Soft-round #32768 popup chrome while TrackPopupMenu is open (Win11 DWM). */
static AppState *g_menu_hook_app;
static HHOOK g_menu_hook;

static BOOL CALLBACK dabom_enum_menu_hwnd(HWND hwnd, LPARAM lParam)
{
    wchar_t cls[32];
    DWORD pref;
    COLORREF border = (COLORREF)lParam;
    if (!GetClassNameW(hwnd, cls, (int)(sizeof(cls) / sizeof(cls[0]))))
        return TRUE;
    if (wcscmp(cls, L"#32768") != 0)
        return TRUE;
    pref = DWMWCP_ROUNDSMALL;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    return TRUE;
}

static void dabom_style_open_menu_windows(AppState *app)
{
    COLORREF bg, text, muted, sep, hot, border;
    if (!app) return;
    dabom_menu_colors(app, &bg, &text, &muted, &sep, &hot, &border);
    EnumThreadWindows(GetCurrentThreadId(), dabom_enum_menu_hwnd, (LPARAM)border);
}

static LRESULT CALLBACK dabom_menu_msg_filter(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == MSGF_MENU && g_menu_hook_app)
        dabom_style_open_menu_windows(g_menu_hook_app);
    return CallNextHookEx(g_menu_hook, code, wParam, lParam);
}

BOOL dabom_track_popup_menu(AppState *app, HMENU menu, UINT flags, int x, int y)
{
    BOOL r;
    if (!app || !menu || !app->hwnd)
        return FALSE;
    g_menu_hook_app = app;
    g_menu_hook = SetWindowsHookExW(WH_MSGFILTER, dabom_menu_msg_filter,
        NULL, GetCurrentThreadId());
    r = TrackPopupMenu(menu, flags, x, y, 0, app->hwnd, NULL);
    if (g_menu_hook) {
        UnhookWindowsHookEx(g_menu_hook);
        g_menu_hook = NULL;
    }
    g_menu_hook_app = NULL;
    return r;
}

static int menubar_item_enabled(AppState *app, int id)
{
    int has = app->file_count > 0;
    switch (id) {
    case ID_BTN_FILE:
        return 1;
    case ID_BTN_PREV:
    case ID_BTN_NEXT:
    case ID_BTN_SLIDE:
        return has;
    case ID_BTN_ZOOM_IN:
    case ID_BTN_ZOOM_OUT:
    case ID_BTN_FIT:
    case ID_BTN_ACTUAL:
        return app->mode == VIEW_FULL && app->full_bmp != NULL;
    default:
        return 0;
    }
}

static const wchar_t *menubar_item_label(AppState *app, int id, const wchar_t *base)
{
    if (id == ID_BTN_SLIDE)
        return app->slideshow ? L"슬라이드 중지" : L"슬라이드쇼";
    return base;
}

static void invalidate_menubar(AppState *app)
{
    RECT tr;
    int mh;
    if (!app->hwnd || app->fullscreen)
        return;
    mh = menubar_height(app);
    if (mh <= 0)
        return;
    GetClientRect(app->hwnd, &tr);
    tr.bottom = mh;
    InvalidateRect(app->hwnd, &tr, FALSE);
}

static void menubar_rebuild_layout(AppState *app, HDC hdc)
{
    static const MbDef defs[] = {
        { ID_BTN_FILE,     L"파일",     1 },
        { 0,               NULL,        0 },
        { ID_BTN_PREV,     L"이전",     0 },
        { 0,               NULL,        0 },
        { ID_BTN_NEXT,     L"다음",     0 },
        { 0,               NULL,        0 },
        { ID_BTN_ZOOM_OUT, L"축소",     0 },
        { 0,               NULL,        0 },
        { ID_BTN_ZOOM_IN,  L"확대",     0 },
        { 0,               NULL,        0 },
        { ID_BTN_FIT,      L"맞춤",     0 },
        { 0,               NULL,        0 },
        { ID_BTN_ACTUAL,   L"원본크기", 0 },
        { 0,               NULL,        0 },
        { ID_BTN_SLIDE,    L"슬라이드쇼", 0 },
    };
    const int pad_x = DABOM_SCALE(app, 10);
    const int gap = DABOM_SCALE(app, 2);
    const int sep_w = DABOM_SCALE(app, 9);
    const int mh = menubar_height(app);
    int x = DABOM_SCALE(app, 6);
    int i, n = 0;
    HFONT oldf;

    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    app->tb_count = 0;

    for (i = 0; i < (int)(sizeof(defs) / sizeof(defs[0])); i++) {
        RECT r;
        if (n >= DABOM_TB_MAX)
            break;
        if (defs[i].id == 0) {
            r.left = x;
            r.top = 0;
            r.right = x + sep_w;
            r.bottom = mh;
            app->tb_rc[n] = r;
            app->tb_id[n] = 0;
            app->tb_enabled[n] = 0;
            n++;
            x += sep_w;
            continue;
        }
        {
            const wchar_t *lab = menubar_item_label(app, defs[i].id, defs[i].label);
            SIZE sz;
            int text_w;
            GetTextExtentPoint32W(hdc, lab, (int)wcslen(lab), &sz);
            text_w = sz.cx;
            if (defs[i].file_chevron)
                text_w += DABOM_SCALE(app, 12);
            r.left = x;
            r.top = 0;
            r.right = x + pad_x * 2 + text_w;
            r.bottom = mh;
            app->tb_rc[n] = r;
            app->tb_id[n] = defs[i].id;
            app->tb_enabled[n] = menubar_item_enabled(app, defs[i].id);
            n++;
            x = r.right + gap;
        }
    }
    app->tb_count = n;
    SelectObject(hdc, oldf);
}

static void menubar_ensure_layout(AppState *app)
{
    HDC hdc;
    if (!app->hwnd)
        return;
    hdc = GetDC(app->hwnd);
    if (!hdc)
        return;
    menubar_rebuild_layout(app, hdc);
    ReleaseDC(app->hwnd, hdc);
}

static int hit_menubar(AppState *app, int mx, int my)
{
    int i;
    int mh = menubar_height(app);
    if (mh <= 0 || my < 0 || my >= mh)
        return -1;
    if (app->tb_count <= 0)
        menubar_ensure_layout(app);
    for (i = 0; i < app->tb_count; i++) {
        if (app->tb_id[i] == 0)
            continue;
        if (mx >= app->tb_rc[i].left && mx < app->tb_rc[i].right &&
            my >= app->tb_rc[i].top && my < app->tb_rc[i].bottom) {
            if (!app->tb_enabled[i])
                return -1;
            return i;
        }
    }
    return -1;
}

static void paint_menubar(AppState *app, HDC hdc, int w)
{
    int mh = menubar_height(app);
    RECT tr = { 0, 0, w, mh };
    HBRUSH br;
    HPEN pen, op;
    HFONT oldf;
    int i;
    COLORREF hover_bg;

    if (mh <= 0)
        return;

    menubar_rebuild_layout(app, hdc);

    br = CreateSolidBrush(app->palette.surface);
    FillRect(hdc, &tr, br);
    DeleteObject(br);

    pen = CreatePen(PS_SOLID, 1, app->palette.border);
    op = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, 0, mh - 1, NULL);
    LineTo(hdc, w, mh - 1);
    SelectObject(hdc, op);
    DeleteObject(pen);

    hover_bg = app->palette.dark ? RGB(0x2A, 0x2D, 0x2E) : RGB(0xE8, 0xE8, 0xE8);

    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);

    for (i = 0; i < app->tb_count; i++) {
        RECT r = app->tb_rc[i];
        if (app->tb_id[i] == 0) {
            int sx = (r.left + r.right) / 2;
            int y0 = r.top + DABOM_SCALE(app, 8);
            int y1 = r.bottom - DABOM_SCALE(app, 8);
            HPEN sp = CreatePen(PS_SOLID, 1, app->palette.border);
            HPEN osp = (HPEN)SelectObject(hdc, sp);
            MoveToEx(hdc, sx, y0, NULL);
            LineTo(hdc, sx, y1);
            SelectObject(hdc, osp);
            DeleteObject(sp);
            continue;
        }
        {
            const wchar_t *lab;
            SIZE sz;
            int tx, ty;
            RECT hr;
            int hot = (i == app->tb_hover && app->tb_enabled[i]);

            if (app->tb_id[i] == ID_BTN_SLIDE)
                lab = menubar_item_label(app, ID_BTN_SLIDE, L"슬라이드쇼");
            else if (app->tb_id[i] == ID_BTN_FILE)
                lab = L"파일";
            else if (app->tb_id[i] == ID_BTN_PREV)
                lab = L"이전";
            else if (app->tb_id[i] == ID_BTN_NEXT)
                lab = L"다음";
            else if (app->tb_id[i] == ID_BTN_ZOOM_OUT)
                lab = L"축소";
            else if (app->tb_id[i] == ID_BTN_ZOOM_IN)
                lab = L"확대";
            else if (app->tb_id[i] == ID_BTN_FIT)
                lab = L"맞춤";
            else if (app->tb_id[i] == ID_BTN_ACTUAL)
                lab = L"원본크기";
            else
                lab = L"";

            if (hot) {
                hr = r;
                hr.top += DABOM_SCALE(app, 3);
                hr.bottom -= DABOM_SCALE(app, 4);
                hr.left += DABOM_SCALE(app, 2);
                hr.right -= DABOM_SCALE(app, 2);
                br = CreateSolidBrush(hover_bg);
                FillRect(hdc, &hr, br);
                DeleteObject(br);
            }

            SetTextColor(hdc, app->tb_enabled[i] ? app->palette.text : app->palette.muted);
            GetTextExtentPoint32W(hdc, lab, (int)wcslen(lab), &sz);
            tx = r.left + DABOM_SCALE(app, 10);
            ty = r.top + (mh - sz.cy) / 2;
            TextOutW(hdc, tx, ty, lab, (int)wcslen(lab));

            if (app->tb_id[i] == ID_BTN_FILE) {
                const wchar_t *ch = L"▾";
                SetTextColor(hdc, app->palette.muted);
                TextOutW(hdc, tx + sz.cx + DABOM_SCALE(app, 4), ty, ch, 1);
            }
        }
    }
    SelectObject(hdc, oldf);
}

static void menubar_set_hover(AppState *app, int idx)
{
    if (app->tb_hover == idx)
        return;
    app->tb_hover = idx;
    invalidate_menubar(app);
}

enum { FILE_POPUP_MAX = 8 };

/* Custom WS_POPUP 파일 드롭다운 — TrackPopupMenu chrome 없이 Cursor File 메뉴 매칭. */
typedef struct {
    AppState *app;
    int hover; /* row index or -1 */
    int width, height;
    int item_y[FILE_POPUP_MAX];
    int item_h[FILE_POPUP_MAX];
    int count;
    UINT chosen;
    int done;
} FilePopupState;

static ATOM g_file_popup_atom;

void dabom_paint_menu_row(AppState *app, HDC hdc, const RECT *rc,
    const MenuOdItem *it, int selected, int checked, int disabled, int left_gutter)
{
    COLORREF bg, text, muted, sep, hot, border;
    int pad_right = DABOM_SCALE(app, 14);
    int sep_inset = DABOM_SCALE(app, 12);
    HBRUSH br;
    HFONT oldf;
    COLORREF label_c, mute_c;
    RECT r = *rc;

    dabom_menu_colors(app, &bg, &text, &muted, &sep, &hot, &border);
    (void)border;
    label_c = disabled ? muted : text;
    mute_c = muted;

    br = CreateSolidBrush(bg);
    FillRect(hdc, &r, br);
    DeleteObject(br);

    if (!it || !it->label) {
        int mid = (r.top + r.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, sep);
        HPEN oldp = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, r.left + sep_inset, mid, NULL);
        LineTo(hdc, r.right - sep_inset, mid);
        SelectObject(hdc, oldp);
        DeleteObject(pen);
        return;
    }

    if (selected && !disabled) {
        RECT hr = r;
        int rad = DABOM_SCALE(app, 4);
        HRGN rgn;
        InflateRect(&hr, -DABOM_SCALE(app, 4), -DABOM_SCALE(app, 1));
        if (hr.right > hr.left && hr.bottom > hr.top) {
            rgn = CreateRoundRectRgn(hr.left, hr.top, hr.right + 1, hr.bottom + 1,
                rad * 2, rad * 2);
            br = CreateSolidBrush(hot);
            FillRgn(hdc, rgn, br);
            DeleteObject(br);
            DeleteObject(rgn);
        }
    }

    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);

    if (checked && left_gutter > DABOM_SCALE(app, 14)) {
        SIZE csz;
        const wchar_t *mark = L"✓";
        GetTextExtentPoint32W(hdc, mark, 1, &csz);
        SetTextColor(hdc, label_c);
        TextOutW(hdc,
            r.left + (left_gutter - csz.cx) / 2,
            r.top + (r.bottom - r.top - csz.cy) / 2,
            mark, 1);
    }

    {
        RECT rlab = r;
        rlab.left += left_gutter;
        rlab.right -= pad_right + (it->submenu ? DABOM_SCALE(app, 12) : 0);
        SetTextColor(hdc, label_c);
        DrawTextW(hdc, it->label, -1, &rlab,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (it->submenu) {
        RECT rch = r;
        rch.left = r.right - DABOM_SCALE(app, 22);
        rch.right = r.right - DABOM_SCALE(app, 8);
        SetTextColor(hdc, mute_c);
        DrawTextW(hdc, L"›", -1, &rch,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (it->shortcut) {
        RECT rsc = r;
        rsc.left += left_gutter;
        rsc.right -= pad_right;
        SetTextColor(hdc, mute_c);
        DrawTextW(hdc, it->shortcut, -1, &rsc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdc, oldf);
}

static void file_popup_layout(AppState *app, FilePopupState *st)
{
    HDC hdc;
    HFONT oldf;
    int i, n;
    int left_pad = DABOM_SCALE(app, 12);
    int right_pad = DABOM_SCALE(app, 16);
    int gap = DABOM_SCALE(app, 40);
    int item_h = DABOM_SCALE(app, 28);
    int sep_h = DABOM_SCALE(app, 9);
    int pad_y = DABOM_SCALE(app, 5);
    int max_lab = 0, max_sc = 0, y;

    n = (int)(sizeof(g_file_items) / sizeof(g_file_items[0]));
    if (n > FILE_POPUP_MAX)
        n = FILE_POPUP_MAX;
    st->count = n;

    hdc = GetDC(app->hwnd);
    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    for (i = 0; i < n; i++) {
        SIZE sz;
        if (!g_file_items[i].label)
            continue;
        GetTextExtentPoint32W(hdc, g_file_items[i].label,
            (int)wcslen(g_file_items[i].label), &sz);
        if (sz.cx > max_lab)
            max_lab = sz.cx;
        if (g_file_items[i].shortcut) {
            GetTextExtentPoint32W(hdc, g_file_items[i].shortcut,
                (int)wcslen(g_file_items[i].shortcut), &sz);
            if (sz.cx > max_sc)
                max_sc = sz.cx;
        }
    }
    SelectObject(hdc, oldf);
    ReleaseDC(app->hwnd, hdc);

    st->width = left_pad + max_lab + (max_sc ? gap + max_sc : 0) + right_pad;
    if (st->width < DABOM_SCALE(app, 200))
        st->width = DABOM_SCALE(app, 200);

    y = pad_y;
    for (i = 0; i < n; i++) {
        st->item_y[i] = y;
        st->item_h[i] = g_file_items[i].label ? item_h : sep_h;
        y += st->item_h[i];
    }
    st->height = y + pad_y;
}

static int file_popup_hit(const FilePopupState *st, int y)
{
    int i;
    for (i = 0; i < st->count; i++) {
        if (!g_file_items[i].label)
            continue;
        if (y >= st->item_y[i] && y < st->item_y[i] + st->item_h[i])
            return i;
    }
    return -1;
}

static int file_popup_row_disabled(AppState *app, int idx)
{
    if (idx < 0 || idx >= (int)(sizeof(g_file_ids) / sizeof(g_file_ids[0])))
        return 1;
    if (!g_file_items[idx].label)
        return 1;
    if (g_file_ids[idx] == ID_MENU_FILE_INFO && app->file_count <= 0)
        return 1;
    return 0;
}

static void file_popup_paint(HWND hwnd, FilePopupState *st)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    AppState *app = st->app;
    COLORREF bg, text, muted, sep, hot, border;
    RECT rc;
    HBRUSH br;
    int i;
    int gutter = DABOM_SCALE(app, 12);

    GetClientRect(hwnd, &rc);
    dabom_menu_colors(app, &bg, &text, &muted, &sep, &hot, &border);
    (void)text; (void)muted; (void)sep; (void)hot;

    br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    for (i = 0; i < st->count; i++) {
        RECT ir;
        int sel = (i == st->hover);
        int dis = file_popup_row_disabled(app, i);
        ir.left = 0;
        ir.right = st->width;
        ir.top = st->item_y[i];
        ir.bottom = st->item_y[i] + st->item_h[i];
        dabom_paint_menu_row(app, hdc, &ir, &g_file_items[i], sel, 0, dis, gutter);
    }

    /* Thin inner edge so light/dark popups read as a floating panel. */
    {
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HPEN oldp = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldb = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, 0, 0, rc.right, rc.bottom,
            DABOM_SCALE(app, 8), DABOM_SCALE(app, 8));
        SelectObject(hdc, oldb);
        SelectObject(hdc, oldp);
        DeleteObject(pen);
    }
    EndPaint(hwnd, &ps);
}

static void file_popup_apply_chrome(HWND hwnd, AppState *app)
{
    COLORREF bg, text, muted, sep, hot, border;
    DWORD pref = DWMWCP_ROUNDSMALL;
    dabom_menu_colors(app, &bg, &text, &muted, &sep, &hot, &border);
    (void)bg; (void)text; (void)muted; (void)sep; (void)hot;
    /* Prefer DWM round + CS_DROPSHADOW (SetWindowRgn kills the soft shadow). */
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

static LRESULT CALLBACK FilePopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FilePopupState *st = (FilePopupState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        st = (FilePopupState *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        if (st && st->app)
            file_popup_apply_chrome(hwnd, st->app);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (st)
            file_popup_paint(hwnd, st);
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int hit;
        RECT cr;
        if (!st) return 0;
        GetClientRect(hwnd, &cr);
        if (x < cr.left || x >= cr.right || y < cr.top || y >= cr.bottom)
            hit = -1;
        else
            hit = file_popup_hit(st, y);
        if (hit != st->hover) {
            st->hover = hit;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int y = GET_Y_LPARAM(lParam);
        int hit;
        if (!st) return 0;
        hit = file_popup_hit(st, y);
        if (hit >= 0 && !file_popup_row_disabled(st->app, hit)) {
            st->chosen = g_file_ids[hit];
            st->done = 1;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (!st) return 0;
        if (wParam == VK_ESCAPE) {
            st->done = 1;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_UP) {
            int dir = (wParam == VK_DOWN) ? 1 : -1;
            int i = st->hover;
            int n = st->count;
            int guard = n + 1;
            while (guard-- > 0) {
                i += dir;
                if (i < 0) i = n - 1;
                if (i >= n) i = 0;
                if (g_file_items[i].label && !file_popup_row_disabled(st->app, i)) {
                    st->hover = i;
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
            }
            return 0;
        }
        if (wParam == VK_RETURN && st->hover >= 0 &&
            !file_popup_row_disabled(st->app, st->hover)) {
            st->chosen = g_file_ids[st->hover];
            st->done = 1;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_ACTIVATE:
        if (st && LOWORD(wParam) == WA_INACTIVE) {
            st->done = 1;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ensure_file_popup_class(void)
{
    WNDCLASSW wc;
    if (g_file_popup_atom)
        return;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = FilePopupWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"DaBomFilePopup";
    g_file_popup_atom = RegisterClassW(&wc);
}

static void popup_file_menu(AppState *app, int client_left, int client_bottom)
{
    FilePopupState st;
    HWND hwnd;
    POINT pt;
    MSG msg;
    UINT chosen;

    if (!app || !app->hwnd)
        return;

    ensure_file_popup_class();
    ZeroMemory(&st, sizeof(st));
    st.app = app;
    st.hover = -1;
    file_popup_layout(app, &st);

    pt.x = client_left;
    pt.y = client_bottom;
    ClientToScreen(app->hwnd, &pt);

    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"DaBomFilePopup", L"",
        WS_POPUP,
        pt.x, pt.y, st.width, st.height,
        app->hwnd, NULL, GetModuleHandleW(NULL), &st);
    if (!hwnd)
        return;

    file_popup_apply_chrome(hwnd, app);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetCapture(hwnd);

    while (!st.done && IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0)) {
        /* With capture, button messages arrive on hwnd; client coords outside = dismiss. */
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN) {
            POINT cpt;
            RECT cr;
            cpt.x = GET_X_LPARAM(msg.lParam);
            cpt.y = GET_Y_LPARAM(msg.lParam);
            if (msg.hwnd == hwnd) {
                GetClientRect(hwnd, &cr);
                if (!PtInRect(&cr, cpt)) {
                    st.done = 1;
                    DestroyWindow(hwnd);
                    continue;
                }
            } else {
                ClientToScreen(msg.hwnd ? msg.hwnd : app->hwnd, &cpt);
                {
                    RECT wr;
                    GetWindowRect(hwnd, &wr);
                    if (!PtInRect(&wr, cpt)) {
                        st.done = 1;
                        DestroyWindow(hwnd);
                        continue;
                    }
                }
            }
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.done = 1;
            DestroyWindow(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (GetCapture() == hwnd)
        ReleaseCapture();
    if (IsWindow(hwnd))
        DestroyWindow(hwnd);

    chosen = st.chosen;
    if (chosen)
        SendMessageW(app->hwnd, WM_COMMAND, MAKEWPARAM(chosen, 0), 0);
}

static void dabom_update_menubar(AppState *app)
{
    /* Enable state + 슬라이드 라벨 are painted; refresh strip. */
    if (app->hwnd && !app->fullscreen)
        invalidate_menubar(app);
}

/* Client flat menubar + custom 파일 WS_POPUP (no Win32 frame SetMenu). */
static void create_menubar(AppState *app)
{
    /* Ensure no stock white NC menubar remains. */
    if (app->hwnd && GetMenu(app->hwnd))
        SetMenu(app->hwnd, NULL);
    app->hmenu = NULL;
    app->tb_count = 0;
    app->tb_hover = -1;
    app->tb_tracking = 0;
    if (app->menu_file) {
        DestroyMenu(app->menu_file);
        app->menu_file = NULL;
    }
    dabom_update_menubar(app);
}

static void destroy_menubar(AppState *app)
{
    if (app->hwnd && GetMenu(app->hwnd))
        SetMenu(app->hwnd, NULL);
    app->hmenu = NULL;
    if (app->menu_file) {
        DestroyMenu(app->menu_file);
        app->menu_file = NULL;
    }
    if (app->br_menu) {
        DeleteObject(app->br_menu);
        app->br_menu = NULL;
    }
    app->tb_count = 0;
    app->tb_hover = -1;
    app->tb_tracking = 0;
}

void dabom_measure_od_menu(AppState *app, MEASUREITEMSTRUCT *mis)
{
    const MenuOdItem *it = (const MenuOdItem *)mis->itemData;
    HDC hdc;
    HFONT oldf;
    SIZE sz;
    int check_col = DABOM_SCALE(app, 22);
    int pad_right = DABOM_SCALE(app, 16);
    int gap = DABOM_SCALE(app, 36);
    int sc_w = 0;
    int lab_w = 0;
    int trail = 0;

    if (!it) {
        mis->itemWidth = (UINT)DABOM_SCALE(app, 180);
        mis->itemHeight = (UINT)DABOM_SCALE(app, 28);
        return;
    }
    if (!it->label) {
        mis->itemWidth = (UINT)DABOM_SCALE(app, 180);
        mis->itemHeight = (UINT)DABOM_SCALE(app, 9);
        return;
    }

    hdc = GetDC(app->hwnd);
    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(hdc, it->label, (int)wcslen(it->label), &sz);
    lab_w = sz.cx;
    if (it->submenu) {
        trail = DABOM_SCALE(app, 14);
    } else if (it->shortcut) {
        GetTextExtentPoint32W(hdc, it->shortcut, (int)wcslen(it->shortcut), &sz);
        sc_w = sz.cx;
        trail = sc_w;
    }
    SelectObject(hdc, oldf);
    ReleaseDC(app->hwnd, hdc);

    mis->itemWidth = (UINT)(check_col + lab_w + (trail ? gap + trail : 0) + pad_right);
    mis->itemHeight = (UINT)DABOM_SCALE(app, 28);
}

void dabom_draw_od_menu(AppState *app, const DRAWITEMSTRUCT *dis)
{
    const MenuOdItem *it = (const MenuOdItem *)dis->itemData;
    int selected = (dis->itemState & ODS_SELECTED) != 0;
    int checked = (dis->itemState & ODS_CHECKED) != 0;
    int disabled = (dis->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    int check_col = DABOM_SCALE(app, 22);
    dabom_paint_menu_row(app, dis->hDC, &dis->rcItem, it,
        selected, checked, disabled, check_col);
}

typedef struct {
    AppState *app;
    HWND hwnd;
} AboutDlg;

static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AboutDlg *dlg = (AboutDlg *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        HWND btn;
        dlg = (AboutDlg *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
        dlg->hwnd = hwnd;
        btn = CreateWindowExW(0, L"BUTTON", L"확인",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDOK, GetModuleHandleW(NULL), NULL);
        if (btn && dlg->app && dlg->app->font_ui)
            SendMessageW(btn, WM_SETFONT, (WPARAM)dlg->app->font_ui, TRUE);
        return 0;
    }
    case WM_SIZE: {
        AppState *app = dlg ? dlg->app : NULL;
        int bw = app ? DABOM_SCALE(app, 96) : 96;
        int bh = app ? DABOM_SCALE(app, 28) : 28;
        int pad = app ? DABOM_SCALE(app, 16) : 16;
        RECT rc;
        GetClientRect(hwnd, &rc);
        SetWindowPos(GetDlgItem(hwnd, IDOK), NULL,
            (rc.right - bw) / 2, rc.bottom - pad - bh, bw, bh,
            SWP_NOZORDER);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        AppState *app = dlg ? dlg->app : NULL;
        RECT rc;
        const wchar_t *line1 = L"Windows 네이티브 사진 뷰어";
        const wchar_t *line2 = L"Win32 + WIC";
        wchar_t ver[64];
        SIZE sz;
        int cx, logo_w, logo_h, x, y;

        GetClientRect(hwnd, &rc);
        if (app) {
            HBRUSH br = CreateSolidBrush(app->palette.bg);
            FillRect(hdc, &rc, br);
            DeleteObject(br);

            _snwprintf(ver, 64, L"DaBom %s", DABOM_VERSION_W);
            ver[63] = 0;
            cx = (rc.left + rc.right) / 2;
            logo_w = DABOM_SCALE(app, 280);
            dabom_ensure_logo(app);
            y = DABOM_SCALE(app, 28);
            if (app->logo_bmp && app->logo_w > 0 && app->logo_h > 0) {
                logo_h = (int)((LONGLONG)logo_w * app->logo_h / app->logo_w);
                if (logo_h < 1) logo_h = 1;
                x = cx - logo_w / 2;
                dabom_draw_logo(app, hdc, x, y, logo_w, logo_h);
                y += logo_h + DABOM_SCALE(app, 16);
            }

            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, app->font_ui);
            SetTextColor(hdc, app->palette.text);
            GetTextExtentPoint32W(hdc, ver, (int)wcslen(ver), &sz);
            TextOutW(hdc, cx - sz.cx / 2, y, ver, (int)wcslen(ver));
            y += sz.cy + DABOM_SCALE(app, 10);

            SetTextColor(hdc, app->palette.muted);
            GetTextExtentPoint32W(hdc, line1, (int)wcslen(line1), &sz);
            TextOutW(hdc, cx - sz.cx / 2, y, line1, (int)wcslen(line1));
            y += sz.cy + DABOM_SCALE(app, 4);
            GetTextExtentPoint32W(hdc, line2, (int)wcslen(line2), &sz);
            TextOutW(hdc, cx - sz.cx / 2, y, line2, (int)wcslen(line2));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void show_about(AppState *app)
{
    static ATOM about_atom = 0;
    AboutDlg dlg;
    HWND hwnd;
    MSG msg;
    int ww, wh;
    RECT prc, rc;
    WNDCLASSEXW wc;

    if (!app || !app->hwnd)
        return;

    if (!about_atom) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = AboutWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"DaBomAbout";
        wc.hIcon = app->hicon_sm ? app->hicon_sm
            : (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APPICON),
                IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        about_atom = RegisterClassExW(&wc);
        if (!about_atom)
            return;
    }

    ZeroMemory(&dlg, sizeof(dlg));
    dlg.app = app;
    ww = DABOM_SCALE(app, 420);
    wh = DABOM_SCALE(app, 320);
    GetWindowRect(app->hwnd, &prc);
    rc.left = prc.left + ((prc.right - prc.left) - ww) / 2;
    rc.top = prc.top + ((prc.bottom - prc.top) - wh) / 2;

    EnableWindow(app->hwnd, FALSE);
    hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"DaBomAbout", L"프로그램 정보",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        rc.left, rc.top, ww, wh,
        app->hwnd, NULL, GetModuleHandleW(NULL), &dlg);
    if (!hwnd) {
        EnableWindow(app->hwnd, TRUE);
        return;
    }

    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
        (LPARAM)(app->hicon_sm ? app->hicon_sm : app->hicon_big));
    while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(app->hwnd, TRUE);
    SetForegroundWindow(app->hwnd);
}

static void show_file_info(AppState *app)
{
    wchar_t meta[256];
    wchar_t buf[DABOM_MAX_PATH + 320];
    const wchar_t *path;
    const wchar_t *name;

    if (app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"열린 이미지가 없습니다.", L"파일 정보",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    if (app->mode != VIEW_FULL || !app->full_bmp)
        dabom_load_current_full(app);
    build_image_meta(app, path, meta, 256);
    _snwprintf(buf, DABOM_MAX_PATH + 320,
        L"%s\n(%d / %d)\n\n%s\n\n%s",
        name, app->current + 1, app->file_count,
        meta[0] ? meta : L"(메타데이터 없음)",
        path);
    buf[DABOM_MAX_PATH + 319] = 0;
    MessageBoxW(app->hwnd, buf, L"파일 정보", MB_OK | MB_ICONINFORMATION);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppState *app = &g_app;

    switch (msg) {
    case WM_CREATE:
        app->hwnd = hwnd;
        app->dpi = dabom_get_dpi(hwnd);
        if (!app->page_mode) app->page_mode = PAGE_ONE;
        /* Recreate fonts now that window DPI is known (PerMonitorV2). */
        dabom_create_fonts(app);
        dabom_theme_apply(app, 1);
        /* Title bar / taskbar icons from embedded IDI_APPICON. */
        {
            HINSTANCE hi = ((LPCREATESTRUCTW)lParam)->hInstance;
            app->hicon_big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APPICON),
                IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
            app->hicon_sm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APPICON),
                IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
            if (app->hicon_big)
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)app->hicon_big);
            if (app->hicon_sm)
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)app->hicon_sm);
        }
        create_menubar(app);
        dabom_ctxmenu_create(app);
        app->status_left[0] = 0;
        app->status_right[0] = 0;
        app->status = CreateStatusWindowW(
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            L"", hwnd, DABOM_STATUS_ID);
        if (app->status) {
            SetWindowTheme(app->status, L"", L"");
            SendMessageW(app->status, WM_SETFONT, (WPARAM)app->font_status, TRUE);
            SendMessageW(app->status, SB_SETBKCOLOR, 0, (LPARAM)app->palette.surface);
        }
        layout_statusbar(app);
        dabom_update_chrome(app);
        return 0;

    case WM_DPICHANGED: {
        /* LOWORD/HIWORD both carry the new DPI; lParam = suggested window rect. */
        UINT new_dpi = (UINT)HIWORD(wParam);
        const RECT *suggested = (const RECT *)lParam;
        if (!new_dpi)
            new_dpi = (UINT)LOWORD(wParam);
        dabom_on_dpi_changed(app, new_dpi, suggested);
        return 0;
    }

    case WM_SIZE:
        layout_statusbar(app);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_SETTINGCHANGE:
        /* Apps dark/light toggle: lParam == L"ImmersiveColorSet". */
        if (lParam && lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet") == 0)
            dabom_theme_apply(app, 1);
        /* System UI fonts / metrics (SPI_SETNONCLIENTMETRICS, etc.). */
        if (app->hwnd)
            app->dpi = dabom_get_dpi(app->hwnd);
        dabom_create_fonts(app);
        dabom_apply_fonts(app);
        return 0;

    case WM_THEMECHANGED:
        dabom_theme_apply(app, 1);
        return 0;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
        if (mis && mis->CtlType == ODT_MENU) {
            dabom_measure_od_menu(app, mis);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lParam;
        if (dis && dis->CtlType == ODT_MENU) {
            dabom_draw_od_menu(app, dis);
            return TRUE;
        }
        /* Status bar owner-draw parts (SBT_OWNERDRAW). */
        if (dis && app->status && dis->hwndItem == app->status) {
            draw_status_part(app, dis);
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_all(app);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_FILE: {
            int i;
            int left = 0;
            int mh = menubar_height(app);
            menubar_ensure_layout(app);
            for (i = 0; i < app->tb_count; i++) {
                if (app->tb_id[i] == ID_BTN_FILE) {
                    left = app->tb_rc[i].left;
                    break;
                }
            }
            popup_file_menu(app, left, mh > 0 ? mh : DABOM_SCALE(app, DABOM_MENUBAR_H));
            break;
        }
        case ID_MENU_OPEN_FOLDER: dabom_open_folder_dialog(app); break;
        case ID_MENU_OPEN_FILES:  dabom_open_files_dialog(app); break;
        case ID_MENU_FILE_INFO:   show_file_info(app); break;
        case ID_MENU_ABOUT:       show_about(app); break;
        case ID_MENU_EXIT:        DestroyWindow(hwnd); break;
        case ID_BTN_PREV:   dabom_next(app, -1); break;
        case ID_BTN_NEXT:   dabom_next(app, 1); break;
        case ID_BTN_ZOOM_IN:  dabom_zoom_by(app, 1.25); break;
        case ID_BTN_ZOOM_OUT: dabom_zoom_by(app, 0.8); break;
        case ID_BTN_FIT:    dabom_fit(app); break;
        case ID_BTN_ACTUAL: dabom_actual_size(app); break;
        case ID_BTN_SLIDE:  dabom_toggle_slideshow(app); break;
        default:
            dabom_handle_ctx_command(app, LOWORD(wParam));
            break;
        }
        return 0;

    case WM_CONTEXTMENU: {
        POINT pt;
        RECT content;
        if (lParam == (LPARAM)-1) {
            GetCursorPos(&pt);
        } else {
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
        }
        dabom_get_content_rect(app, &content);
        {
            POINT cpt = pt;
            ScreenToClient(hwnd, &cpt);
            if (cpt.y < content.top || cpt.y > content.bottom)
                break;
        }
        if (app->file_count <= 0 && !app->full_bmp)
            break; /* don't steal when no image */
        dabom_ctxmenu_popup(app, pt.x, pt.y);
        return 0;
    }

    case WM_RBUTTONUP: {
        POINT pt;
        RECT content;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        dabom_get_content_rect(app, &content);
        if (pt.y < content.top || pt.y > content.bottom)
            break;
        if (app->file_count <= 0 && !app->full_bmp)
            break;
        ClientToScreen(hwnd, &pt);
        dabom_ctxmenu_popup(app, pt.x, pt.y);
        return 0;
    }

    case WM_KEYDOWN: {
        int ctrl = GetKeyState(VK_CONTROL) < 0;
        int alt = GetKeyState(VK_MENU) < 0;
        int shift = GetKeyState(VK_SHIFT) < 0;
        switch (wParam) {
        case VK_ESCAPE:
            if (app->fullscreen)
                dabom_toggle_fullscreen(app);
            else if (app->mode == VIEW_FULL)
                dabom_set_mode(app, VIEW_GRID);
            break;
        case VK_LEFT:
        case VK_UP:
            if (app->mode == VIEW_FULL) dabom_next(app, -1);
            break;
        case VK_RIGHT:
        case VK_DOWN:
            if (app->mode == VIEW_FULL) dabom_next(app, 1);
            break;
        case VK_SPACE:
            dabom_toggle_slideshow(app);
            break;
        case VK_OEM_PLUS:
        case VK_ADD:
            dabom_zoom_by(app, 1.25);
            break;
        case VK_OEM_MINUS:
            /* VK_SUBTRACT (0x6D) collides with 'm' — handled via numpad check below */
            dabom_zoom_by(app, 0.8);
            break;
        case VK_RETURN:
            if (alt)
                dabom_toggle_fullscreen(app);
            else if (ctrl)
                dabom_open_explorer(app);
            break;
        case VK_DELETE:
            dabom_delete_current(app);
            break;
        case VK_INSERT:
            dabom_copy_to_library(app);
            break;
        case VK_F12:
            dabom_show_camera_info(app);
            break;
        case '0':
            dabom_fit(app);
            break;
        case '1':
            if (ctrl && alt) dabom_set_page_mode(app, PAGE_ONE);
            else dabom_actual_size(app);
            break;
        case '2':
            if (ctrl && alt) dabom_set_page_mode(app, PAGE_TWO);
            break;
        case '4':
            if (ctrl && alt) dabom_set_page_mode(app, PAGE_FOUR);
            break;
        case 'O':
        case 'o':
            if (ctrl) dabom_open_folder_dialog(app);
            break;
        case 'C':
        case 'c':
            if (ctrl) dabom_copy_current(app);
            break;
        case 'V':
        case 'v':
            if (ctrl) dabom_paste_clipboard(app);
            break;
        case 'S':
        case 's':
            if (ctrl) dabom_save_as_current(app);
            else if (alt) dabom_set_sort_mode(app, SORT_NAME);
            else dabom_toggle_continuous(app);
            break;
        case 'T':
        case 't':
            if (ctrl) dabom_rotate_current(app);
            break;
        case 'R':
        case 'r':
            if (ctrl) dabom_resize_dialog(app);
            break;
        case 'E':
        case 'e':
            if (ctrl && shift) dabom_open_edit_app(app);
            else if (ctrl) dabom_convert_current(app);
            break;
        case 'P':
            /* 'p'(0x70) collides with VK_F1 — only uppercase 'P' from WM_KEYDOWN letters */
            if (ctrl) dabom_print_current(app);
            else if (alt) dabom_toggle_pixel_info(app);
            break;
        case 'M':
            dabom_toggle_smooth(app);
            break;
        case 0x6D: /* VK_SUBTRACT numpad- OR literal 'm' if ever sent */
            if (wParam == VK_SUBTRACT)
                dabom_zoom_by(app, 0.8);
            else
                dabom_toggle_smooth(app);
            break;
        case 'H':
            if (alt) dabom_toggle_hdr(app);
            break;
        case 'B':
            if (alt) dabom_bookmark_current(app);
            break;
        case 'Q':
            if (alt) dabom_toggle_pixel_info(app);
            break;
        case VK_F1:
            show_about(app);
            break;
        case 'X':
            if (!ctrl && !alt)
                DestroyWindow(hwnd);
            break;
        case VK_TAB:
            show_file_info(app);
            break;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int keys = GET_KEYSTATE_WPARAM(wParam);
        if (app->mode == VIEW_GRID) {
            app->scroll_y -= delta;
            if (app->scroll_y < 0) app->scroll_y = 0;
            InvalidateRect(hwnd, NULL, TRUE);
        } else if (app->mode == VIEW_FULL) {
            if (keys & MK_CONTROL) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (delta > 0)
                    dabom_zoom_at(app, 1.25, pt.x, pt.y);
                else if (delta < 0)
                    dabom_zoom_at(app, 0.8, pt.x, pt.y);
            } else if (app->continuous) {
                app->scroll_y -= delta / 4;
                if (app->scroll_y < 0) app->scroll_y = 0;
                InvalidateRect(hwnd, NULL, TRUE);
            } else {
                if (delta < 0)
                    dabom_next(app, 1);
                else if (delta > 0)
                    dabom_next(app, -1);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        int tb = hit_menubar(app, mx, my);
        if (tb >= 0) {
            int id = app->tb_id[tb];
            if (id == ID_BTN_FILE)
                popup_file_menu(app, app->tb_rc[tb].left, menubar_height(app));
            else
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
            return 0;
        }
        /* Navigator close / drag */
        if (app->mode == VIEW_FULL && app->nav_rc.right > app->nav_rc.left) {
            RECT xbtn;
            xbtn.left = app->nav_rc.right - DABOM_SCALE(app, 18);
            xbtn.top = app->nav_rc.top + DABOM_SCALE(app, 2);
            xbtn.right = app->nav_rc.right - DABOM_SCALE(app, 4);
            xbtn.bottom = app->nav_rc.top + DABOM_SCALE(app, 16);
            if (mx >= xbtn.left && mx <= xbtn.right && my >= xbtn.top && my <= xbtn.bottom) {
                app->nav_dismissed = 1;
                InvalidateRect(hwnd, NULL, TRUE);
                return 0;
            }
            if (mx >= app->nav_rc.left && mx <= app->nav_rc.right &&
                my >= app->nav_rc.top && my <= app->nav_rc.bottom) {
                app->nav_dragging = 1;
                app->drag_ox = mx;
                app->drag_oy = my;
                SetCapture(hwnd);
                return 0;
            }
        }
        if (app->mode == VIEW_GRID) {
            int idx = hit_grid_index(app, mx, my);
            if (idx >= 0)
                dabom_goto_index(app, idx);
        } else if (app->mode == VIEW_FULL) {
            app->dragging = 1;
            app->drag_ox = mx;
            app->drag_oy = my;
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        if (!app->tb_tracking && !app->fullscreen) {
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            app->tb_tracking = 1;
        }
        menubar_set_hover(app, hit_menubar(app, mx, my));
        if (app->nav_dragging && app->mode == VIEW_FULL) {
            int ddx = mx - app->drag_ox;
            int ddy = my - app->drag_oy;
            /* Map navigator drag to pan (opposite direction, DPI-scaled thumb). */
            if (app->view_scale > 0 && app->full_w > 0) {
                double nav_inner_w = (double)DABOM_SCALE(app, 148);
                double nav_inner_h = (double)DABOM_SCALE(app, 96);
                double nav_ts = nav_inner_w / (double)app->full_w;
                if ((double)app->full_h * nav_ts > nav_inner_h)
                    nav_ts = nav_inner_h / (double)app->full_h;
                if (nav_ts > 0) {
                    app->pan_x -= (int)(ddx / nav_ts * app->view_scale);
                    app->pan_y -= (int)(ddy / nav_ts * app->view_scale);
                }
            }
            app->drag_ox = mx;
            app->drag_oy = my;
            app->fit_mode = 0;
            InvalidateRect(hwnd, NULL, TRUE);
        } else if (app->dragging && app->mode == VIEW_FULL) {
            app->pan_x += mx - app->drag_ox;
            app->pan_y += my - app->drag_oy;
            app->drag_ox = mx;
            app->drag_oy = my;
            app->fit_mode = 0;
            if (app->layout == LAYOUT_FIT || app->layout == LAYOUT_FIT_LARGE ||
                app->layout == LAYOUT_FIT_WIDTH || app->layout == LAYOUT_FIT_HEIGHT ||
                app->layout == LAYOUT_STRETCH)
                app->layout = LAYOUT_ZOOM_KEEP;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        if (app->pixel_info)
            dabom_sample_pixel_at(app, mx, my);
        return 0;
    }

    case WM_MOUSELEAVE:
        app->tb_tracking = 0;
        menubar_set_hover(app, -1);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (hit_menubar(app, pt.x, pt.y) >= 0) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONUP:
        if (app->dragging || app->nav_dragging) {
            app->dragging = 0;
            app->nav_dragging = 0;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        if (hit_menubar(app, mx, my) >= 0)
            return 0;
        if (app->mode == VIEW_FULL)
            dabom_set_mode(app, VIEW_GRID);
        return 0;
    }

    case WM_TIMER:
        if (wParam == ID_TIMER_SLIDE && app->slideshow)
            dabom_next(app, 1);
        return 0;

    case WM_DESTROY:
        if (app->slideshow)
            KillTimer(hwnd, ID_TIMER_SLIDE);
        dabom_ctxmenu_destroy(app);
        destroy_menubar(app);
        dabom_clear_files(app);
        dabom_free_bitmap(&app->logo_bmp);
        app->logo_w = app->logo_h = 0;
        if (app->hicon_big) {
            DestroyIcon(app->hicon_big);
            app->hicon_big = NULL;
        }
        if (app->hicon_sm) {
            DestroyIcon(app->hicon_sm);
            app->hicon_sm = NULL;
        }
        dabom_wic_shutdown(app);
        if (app->br_status) {
            DeleteObject(app->br_status);
            app->br_status = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* Fallback when SPI_GETNONCLIENTMETRICS fails: stock DEFAULT_GUI_FONT. */
static HFONT dabom_clone_stock_gui_font(void)
{
    HFONT stock = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    LOGFONTW lf;

    ZeroMemory(&lf, sizeof(lf));
    if (stock && GetObjectW(stock, sizeof(lf), &lf))
        return CreateFontIndirectW(&lf);
    return CreateFontW(
        -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void dabom_create_fonts(AppState *app)
{
    NONCLIENTMETRICSW ncm;
    LOGFONTW lf_brand;
    int msg_h, brand_h;
    int ok = 0;
    int used_for_dpi = 0;
    UINT dpi = app->dpi ? app->dpi : DABOM_DPI_DEFAULT;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *PFN_SystemParametersInfoForDpi)(UINT, UINT, PVOID, UINT, UINT);
    PFN_SystemParametersInfoForDpi spi_dpi = NULL;

    if (app->font_ui) {
        DeleteObject(app->font_ui);
        app->font_ui = NULL;
    }
    if (app->font_status) {
        DeleteObject(app->font_status);
        app->font_status = NULL;
    }
    if (app->font_btn) {
        DeleteObject(app->font_btn);
        app->font_btn = NULL;
    }
    if (app->font_brand) {
        DeleteObject(app->font_brand);
        app->font_brand = NULL;
    }

    if (user32)
        spi_dpi = (PFN_SystemParametersInfoForDpi)GetProcAddress(
            user32, "SystemParametersInfoForDpi");

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    /* Prefer DPI-specific metrics so fonts match the window's monitor. */
    if (spi_dpi && spi_dpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
        ok = 1;
        used_for_dpi = 1;
    }
    if (!ok)
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (!ok) {
        /* Older layout without iPaddedBorderWidth */
        ncm.cbSize = sizeof(ncm) - sizeof(int);
        if (spi_dpi && spi_dpi(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0, dpi)) {
            ok = 1;
            used_for_dpi = 1;
        }
        if (!ok)
            ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    }

    if (ok) {
        /* Legacy SPI returns primary-monitor fonts — scale lfHeight to window DPI. */
        if (!used_for_dpi && dpi != DABOM_DPI_DEFAULT) {
            ncm.lfMessageFont.lfHeight =
                MulDiv(ncm.lfMessageFont.lfHeight, (int)dpi, DABOM_DPI_DEFAULT);
            ncm.lfStatusFont.lfHeight =
                MulDiv(ncm.lfStatusFont.lfHeight, (int)dpi, DABOM_DPI_DEFAULT);
        }
        app->font_ui = CreateFontIndirectW(&ncm.lfMessageFont);
        app->font_btn = CreateFontIndirectW(&ncm.lfMessageFont);
        app->font_status = CreateFontIndirectW(&ncm.lfStatusFont);
        lf_brand = ncm.lfMessageFont;
        msg_h = lf_brand.lfHeight;
        if (msg_h == 0)
            msg_h = -MulDiv(12, (int)dpi, DABOM_DPI_DEFAULT);
        brand_h = (msg_h * DABOM_BRAND_FONT_PT_MULT) / DABOM_BRAND_FONT_PT_DIV;
        if (brand_h == 0)
            brand_h = msg_h * 3;
        /* Keep sign of lfHeight (negative = character height). */
        lf_brand.lfHeight = brand_h;
        lf_brand.lfWeight = FW_BOLD;
        app->font_brand = CreateFontIndirectW(&lf_brand);
    } else {
        app->font_ui = dabom_clone_stock_gui_font();
        app->font_btn = dabom_clone_stock_gui_font();
        app->font_status = dabom_clone_stock_gui_font();
        ZeroMemory(&lf_brand, sizeof(lf_brand));
        if (app->font_ui)
            GetObjectW(app->font_ui, sizeof(lf_brand), &lf_brand);
        msg_h = lf_brand.lfHeight ? lf_brand.lfHeight : -12;
        if (dpi != DABOM_DPI_DEFAULT)
            msg_h = MulDiv(msg_h, (int)dpi, DABOM_DPI_DEFAULT);
        lf_brand.lfHeight = (msg_h * DABOM_BRAND_FONT_PT_MULT) / DABOM_BRAND_FONT_PT_DIV;
        lf_brand.lfWeight = FW_BOLD;
        app->font_brand = CreateFontIndirectW(&lf_brand);
    }
}

void dabom_apply_fonts(AppState *app)
{
    if (!app->hwnd)
        return;
    if (app->status)
        SendMessageW(app->status, WM_SETFONT, (WPARAM)app->font_status, TRUE);
    InvalidateRect(app->hwnd, NULL, TRUE);
    if (app->status)
        InvalidateRect(app->status, NULL, TRUE);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;
    int win_w, win_h;

    (void)hPrev;
    (void)cmd;

    /* Must run before any HWND / dialog so common file dialogs stay sharp. */
    dabom_set_dpi_awareness();

    ZeroMemory(&g_app, sizeof(g_app));
    g_app.dpi = dabom_get_dpi(NULL);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        MessageBoxW(NULL, L"COM 초기화 실패", L"DaBom", MB_ICONERROR);
        return 1;
    }

    dabom_config_path(&g_app);
    dabom_load_settings(&g_app);
    dabom_create_fonts(&g_app);
    dabom_theme_apply(&g_app, 1); /* startup: Apps light/dark palette */

    if (!dabom_wic_init(&g_app)) {
        MessageBoxW(NULL, L"WIC 초기화 실패", L"DaBom", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DaBomPhotoViewer";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!wc.hIcon)
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!wc.hIconSm)
        wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"창 클래스 등록 실패", L"DaBom", MB_ICONERROR);
        dabom_wic_shutdown(&g_app);
        CoUninitialize();
        return 1;
    }

    g_app.mode = VIEW_EMPTY;

    /* Initial size authored at 96 DPI → device pixels for current system DPI. */
    win_w = DABOM_SCALE(&g_app, 1100);
    win_h = DABOM_SCALE(&g_app, 740);

    hwnd = CreateWindowExW(
        0, L"DaBomPhotoViewer", DABOM_TITLE_W,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, win_w, win_h,
        NULL, NULL, hInst, NULL);

    if (!hwnd) {
        dabom_wic_shutdown(&g_app);
        CoUninitialize();
        return 1;
    }

    /* optional: reopen last folder */
    if (g_app.last_folder[0] && GetFileAttributesW(g_app.last_folder) != INVALID_FILE_ATTRIBUTES) {
        if (dabom_scan_folder(&g_app, g_app.last_folder) > 0) {
            g_app.current = 0;
            dabom_set_mode(&g_app, VIEW_GRID);
        }
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.font_ui) DeleteObject(g_app.font_ui);
    if (g_app.font_status) DeleteObject(g_app.font_status);
    if (g_app.font_btn) DeleteObject(g_app.font_btn);
    if (g_app.font_brand) DeleteObject(g_app.font_brand);
    if (g_app.br_status) DeleteObject(g_app.br_status);
    CoUninitialize();
    return (int)msg.wParam;
}
