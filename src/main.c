#include "dabom.h"
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
/* Older Win10 builds used attribute 19 for the same immersive dark title bar. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

static AppState g_app;

static void dabom_update_menubar(AppState *app);

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
        /* Charcoal + warm amber (brand accent stays readable). */
        p->bg        = RGB(22, 26, 30);
        p->surface   = RGB(32, 38, 44);
        p->border    = RGB(52, 60, 68);
        p->text      = RGB(240, 236, 228);
        p->muted     = RGB(150, 156, 162);
        p->accent    = RGB(220, 150, 60);
        p->accent_dk = RGB(180, 110, 35);
        p->grid_bg   = RGB(18, 21, 24);
    } else {
        /* Light Apps: cool gray surfaces, deeper amber for contrast. */
        p->bg        = RGB(245, 246, 248);
        p->surface   = RGB(255, 255, 255);
        p->border    = RGB(200, 205, 212);
        p->text      = RGB(28, 32, 36);
        p->muted     = RGB(100, 108, 116);
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

    /* Refresh 파일 popup frame brush (owner-draw items read palette live). */
    if (app->menu_file) {
        MENUINFO mi;
        COLORREF bg = dark ? RGB(0x2D, 0x2D, 0x2D) : RGB(0xFF, 0xFF, 0xFF);
        if (app->br_menu) {
            DeleteObject(app->br_menu);
            app->br_menu = NULL;
        }
        app->br_menu = CreateSolidBrush(bg);
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_STYLE;
        mi.dwStyle = MNS_CHECKORBMP;
        mi.hbrBack = app->br_menu;
        SetMenuInfo(app->menu_file, &mi);
    }

    if (app->hwnd) {
        InvalidateRect(app->hwnd, NULL, TRUE);
        if (app->status)
            InvalidateRect(app->status, NULL, TRUE);
        if (app->status_meta)
            InvalidateRect(app->status_meta, NULL, TRUE);
        if (app->hmenu)
            DrawMenuBar(app->hwnd);
    }
}

static void layout_children(AppState *app)
{
    RECT rc;
    int mid;
    GetClientRect(app->hwnd, &rc);
    mid = rc.right / 2;
    if (mid < 160)
        mid = rc.right > 0 ? rc.right : 0;
    if (app->status)
        MoveWindow(app->status, 8, rc.bottom - DABOM_STATUS_H,
            mid > 8 ? mid - 8 : 0, DABOM_STATUS_H, TRUE);
    if (app->status_meta)
        MoveWindow(app->status_meta, mid, rc.bottom - DABOM_STATUS_H,
            rc.right > mid + 8 ? rc.right - mid - 8 : 0, DABOM_STATUS_H, TRUE);
}

void dabom_set_status(AppState *app, const wchar_t *text)
{
    if (app->status)
        SetWindowTextW(app->status, text ? text : L"");
}

static void dabom_set_status_meta(AppState *app, const wchar_t *text)
{
    if (app->status_meta)
        SetWindowTextW(app->status_meta, text ? text : L"");
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
        _snwprintf(left, DABOM_MAX_PATH + 64, L"%s  (%d / %d)%s",
            name, app->current + 1, app->file_count,
            app->slideshow ? L"  · 슬라이드쇼" : L"");
        left[DABOM_MAX_PATH + 63] = 0;
        build_image_meta(app, path, meta, 256);
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

void dabom_fit(AppState *app)
{
    app->fit_mode = 1;
    app->pan_x = app->pan_y = 0;
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_actual_size(AppState *app)
{
    if (app->mode != VIEW_FULL || !app->full_bmp)
        return;
    app->fit_mode = 0;
    app->zoom = 1.0;
    app->pan_x = app->pan_y = 0;
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_zoom_by(AppState *app, double factor)
{
    RECT rc;
    double zw, zh, fit;
    if (app->mode != VIEW_FULL || !app->full_bmp)
        return;

    GetClientRect(app->hwnd, &rc);
    rc.bottom -= DABOM_STATUS_H;
    zw = (double)(rc.right - rc.left) / (double)app->full_w;
    zh = (double)(rc.bottom - rc.top) / (double)app->full_h;
    fit = zw < zh ? zw : zh;
    if (fit <= 0)
        fit = 1.0;

    if (app->fit_mode) {
        app->zoom = fit;
        app->fit_mode = 0;
    }
    app->zoom *= factor;
    if (app->zoom < DABOM_ZOOM_MIN) app->zoom = DABOM_ZOOM_MIN;
    if (app->zoom > DABOM_ZOOM_MAX) app->zoom = DABOM_ZOOM_MAX;
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

static void get_content_rect(AppState *app, RECT *rc)
{
    /* Menubar is non-client (SetMenu); client starts below it automatically. */
    GetClientRect(app->hwnd, rc);
    rc->bottom -= DABOM_STATUS_H;
}

static void paint_empty(AppState *app, HDC hdc, const RECT *rc)
{
    RECT r = *rc;
    const wchar_t *brand = L"DaBom";
    const wchar_t *ver = DABOM_TITLE_W; /* e.g. DaBom 0.0.4 */
    const wchar_t *sub = L"사진 폴더를 열어보세요";
    const wchar_t *hint = L"폴더 열기 또는 파일 열기로 이미지를 불러옵니다";
    SIZE sz;
    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;

    {
        HBRUSH br = CreateSolidBrush(app->palette.bg);
        FillRect(hdc, &r, br);
        DeleteObject(br);
    }

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, app->font_brand);
    SetTextColor(hdc, app->palette.accent);
    GetTextExtentPoint32W(hdc, brand, (int)wcslen(brand), &sz);
    TextOutW(hdc, cx - sz.cx / 2, cy - 80, brand, (int)wcslen(brand));

    SelectObject(hdc, app->font_ui);
    SetTextColor(hdc, app->palette.muted);
    GetTextExtentPoint32W(hdc, ver, (int)wcslen(ver), &sz);
    TextOutW(hdc, cx - sz.cx / 2, cy - 18, ver, (int)wcslen(ver));

    SetTextColor(hdc, app->palette.text);
    GetTextExtentPoint32W(hdc, sub, (int)wcslen(sub), &sz);
    TextOutW(hdc, cx - sz.cx / 2, cy + 12, sub, (int)wcslen(sub));

    SetTextColor(hdc, app->palette.muted);
    GetTextExtentPoint32W(hdc, hint, (int)wcslen(hint), &sz);
    TextOutW(hdc, cx - sz.cx / 2, cy + 40, hint, (int)wcslen(hint));
}

static void paint_grid(AppState *app, HDC hdc, const RECT *rc)
{
    int cell = DABOM_THUMB + DABOM_THUMB_PAD * 2;
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
        int x = rc->left + col * cell + DABOM_THUMB_PAD;
        int y = rc->top + row * cell + DABOM_THUMB_PAD - app->scroll_y;
        RECT cellrc;
        const wchar_t *name;

        if (y + DABOM_THUMB < rc->top || y > rc->bottom)
            continue;

        dabom_ensure_thumb(app, i);

        cellrc.left = x - 4;
        cellrc.top = y - 4;
        cellrc.right = x + DABOM_THUMB + 4;
        cellrc.bottom = y + DABOM_THUMB + 22;
        RoundRect(hdc, cellrc.left, cellrc.top, cellrc.right, cellrc.bottom, 6, 6);

        if (app->files[i].thumb) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP old = (HBITMAP)SelectObject(mem, app->files[i].thumb);
            int tw = app->files[i].thumb_w;
            int th = app->files[i].thumb_h;
            int dx = x + (DABOM_THUMB - tw) / 2;
            int dy = y + (DABOM_THUMB - th) / 2;
            SetStretchBltMode(hdc, HALFTONE);
            BitBlt(hdc, dx, dy, tw, th, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }

        name = wcsrchr(app->files[i].path, L'\\');
        name = name ? name + 1 : app->files[i].path;
        {
            RECT tr = { x, y + DABOM_THUMB + 2, x + DABOM_THUMB, y + DABOM_THUMB + 20 };
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

static void paint_full(AppState *app, HDC hdc, const RECT *rc)
{
    HBRUSH bg = CreateSolidBrush(app->palette.bg);
    double zw, zh, scale;
    int dw, dh, dx, dy;
    int cw = rc->right - rc->left;
    int ch = rc->bottom - rc->top;

    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    if (!app->full_bmp || app->full_w == 0 || app->full_h == 0) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, app->palette.muted);
        SelectObject(hdc, app->font_ui);
        {
            const wchar_t *err = L"이미지를 불러올 수 없습니다.";
            TextOutW(hdc, rc->left + 24, rc->top + 24, err, (int)wcslen(err));
        }
        return;
    }

    zw = (double)cw / (double)app->full_w;
    zh = (double)ch / (double)app->full_h;
    if (app->fit_mode) {
        scale = zw < zh ? zw : zh;
        if (scale <= 0) scale = 1.0;
        app->zoom = scale;
        app->pan_x = app->pan_y = 0;
    } else {
        scale = app->zoom;
    }

    dw = (int)(app->full_w * scale + 0.5);
    dh = (int)(app->full_h * scale + 0.5);
    dx = rc->left + (cw - dw) / 2 + app->pan_x;
    dy = rc->top + (ch - dh) / 2 + app->pan_y;

    {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP old = (HBITMAP)SelectObject(mem, app->full_bmp);
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
        StretchBlt(hdc, dx, dy, dw, dh, mem, 0, 0, (int)app->full_w, (int)app->full_h, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
}

static void paint_all(AppState *app)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(app->hwnd, &ps);
    RECT rc, content;
    HDC mem;
    HBITMAP dib, old;
    int w, h;

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

    get_content_rect(app, &content);

    /* Fill full client first so status-bar strip matches chrome. */
    {
        HBRUSH br = CreateSolidBrush(app->palette.bg);
        FillRect(mem, &rc, br);
        DeleteObject(br);
    }

    if (app->mode == VIEW_EMPTY)
        paint_empty(app, mem, &content);
    else if (app->mode == VIEW_GRID)
        paint_grid(app, mem, &content);
    else
        paint_full(app, mem, &content);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    EndPaint(app->hwnd, &ps);
}

static int hit_grid_index(AppState *app, int mx, int my)
{
    RECT rc;
    int cell = DABOM_THUMB + DABOM_THUMB_PAD * 2;
    int cols, col, row, idx;
    get_content_rect(app, &rc);
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

/* ---- Win32 non-client menubar (SetMenu) + themed 파일 popup ---- */

typedef struct {
    const wchar_t *label;    /* NULL when separator */
    const wchar_t *shortcut; /* optional right-aligned accelerator */
} FileMenuOd;

enum {
    FILE_MENU_SEP_ID = 1 /* non-command id for owner-draw separator row */
};

/* Lifetime statics — itemData pointers stay valid while menubar exists. */
static const FileMenuOd g_file_items[] = {
    { L"폴더 열기",     NULL },
    { L"파일 열기",     NULL },
    { L"파일 정보",     L"TAB" },
    { NULL,             NULL }, /* separator */
    { L"프로그램 정보", L"F1" },
    { L"종료",          L"X" },
};
static const UINT g_file_ids[] = {
    ID_MENU_OPEN_FOLDER,
    ID_MENU_OPEN_FILES,
    ID_MENU_FILE_INFO,
    FILE_MENU_SEP_ID,
    ID_MENU_ABOUT,
    ID_MENU_EXIT,
};

/* Popup colors follow Windows Apps mode (not classic unthemed menu chrome). */
static void file_menu_colors(const AppState *app,
    COLORREF *bg, COLORREF *text, COLORREF *muted,
    COLORREF *sep, COLORREF *hot_bg, COLORREF *border)
{
    if (app->palette.dark) {
        *bg     = RGB(0x2D, 0x2D, 0x2D);
        *text   = RGB(0xFF, 0xFF, 0xFF);
        *muted  = RGB(0xC0, 0xC0, 0xC0);
        *sep    = RGB(0x55, 0x55, 0x55);
        *hot_bg = RGB(0x3D, 0x3D, 0x3D);
        *border = RGB(0x1A, 0x1A, 0x1A);
    } else {
        *bg     = RGB(0xFF, 0xFF, 0xFF);
        *text   = RGB(0x00, 0x00, 0x00);
        *muted  = RGB(0x55, 0x55, 0x55);
        *sep    = RGB(0xD0, 0xD0, 0xD0);
        *hot_bg = RGB(0xE8, 0xE8, 0xE8);
        *border = RGB(0xC8, 0xC8, 0xC8);
    }
}

static void menu_enable(HMENU menu, UINT id, int on)
{
    EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : (MF_GRAYED | MF_DISABLED)));
}

static void dabom_update_menubar(AppState *app)
{
    int has = app->file_count > 0;
    int zoom_ok = app->mode == VIEW_FULL && app->full_bmp != NULL;
    const wchar_t *slide;

    if (!app->hmenu || !app->hwnd)
        return;

    menu_enable(app->hmenu, ID_BTN_PREV, has);
    menu_enable(app->hmenu, ID_BTN_NEXT, has);
    menu_enable(app->hmenu, ID_BTN_SLIDE, has);
    menu_enable(app->hmenu, ID_BTN_ZOOM_IN, zoom_ok);
    menu_enable(app->hmenu, ID_BTN_ZOOM_OUT, zoom_ok);
    menu_enable(app->hmenu, ID_BTN_FIT, zoom_ok);
    menu_enable(app->hmenu, ID_BTN_ACTUAL, zoom_ok);

    if (app->menu_file) {
        menu_enable(app->menu_file, ID_MENU_FILE_INFO, has);
    }

    slide = app->slideshow ? L"슬라이드 중지" : L"슬라이드쇼";
    ModifyMenuW(app->hmenu, ID_BTN_SLIDE, MF_BYCOMMAND | MF_STRING, ID_BTN_SLIDE, slide);
    DrawMenuBar(app->hwnd);
}

static HMENU create_file_popup(AppState *app)
{
    HMENU menu;
    MENUINFO mi;
    COLORREF bg;
    int i;

    menu = CreatePopupMenu();
    if (!menu)
        return NULL;

    bg = app->palette.dark ? RGB(0x2D, 0x2D, 0x2D) : RGB(0xFF, 0xFF, 0xFF);
    if (app->br_menu) {
        DeleteObject(app->br_menu);
        app->br_menu = NULL;
    }
    app->br_menu = CreateSolidBrush(bg);
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_STYLE;
    mi.dwStyle = MNS_CHECKORBMP;
    mi.hbrBack = app->br_menu;
    SetMenuInfo(menu, &mi);

    for (i = 0; i < (int)(sizeof(g_file_items) / sizeof(g_file_items[0])); i++) {
        MENUITEMINFOW mii;
        ZeroMemory(&mii, sizeof(mii));
        mii.cbSize = sizeof(mii);
        /* Avoid MFT_SEPARATOR alone — it ignores MIIM_DATA and paints classic chrome. */
        mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE;
        mii.fType = MFT_OWNERDRAW;
        mii.fState = g_file_items[i].label ? 0 : (MFS_DISABLED | MFS_GRAYED);
        mii.wID = g_file_ids[i];
        mii.dwItemData = (ULONG_PTR)&g_file_items[i];
        InsertMenuItemW(menu, (UINT)i, TRUE, &mii);
    }
    return menu;
}

/* Real window-frame menubar: 파일 popup + flat top-level view commands. */
static void create_menubar(AppState *app)
{
    HMENU bar;
    HMENU file;

    bar = CreateMenu();
    if (!bar)
        return;

    file = create_file_popup(app);
    if (!file) {
        DestroyMenu(bar);
        return;
    }

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"파일");
    AppendMenuW(bar, MF_STRING, ID_BTN_PREV, L"이전");
    AppendMenuW(bar, MF_STRING, ID_BTN_NEXT, L"다음");
    AppendMenuW(bar, MF_STRING, ID_BTN_ZOOM_OUT, L"축소");
    AppendMenuW(bar, MF_STRING, ID_BTN_ZOOM_IN, L"확대");
    AppendMenuW(bar, MF_STRING, ID_BTN_FIT, L"맞춤");
    AppendMenuW(bar, MF_STRING, ID_BTN_ACTUAL, L"원본크기");
    AppendMenuW(bar, MF_STRING, ID_BTN_SLIDE, L"슬라이드쇼");

    app->menu_file = file;
    app->hmenu = bar;
    SetMenu(app->hwnd, bar);
    DrawMenuBar(app->hwnd);
    dabom_update_menubar(app);
}

static void destroy_menubar(AppState *app)
{
    if (app->hwnd && app->hmenu)
        SetMenu(app->hwnd, NULL);
    if (app->hmenu) {
        DestroyMenu(app->hmenu); /* also destroys popup children */
        app->hmenu = NULL;
        app->menu_file = NULL;
    }
    if (app->br_menu) {
        DeleteObject(app->br_menu);
        app->br_menu = NULL;
    }
}

static void measure_file_menu_item(AppState *app, MEASUREITEMSTRUCT *mis)
{
    const FileMenuOd *it = (const FileMenuOd *)mis->itemData;
    HDC hdc;
    HFONT oldf;
    SIZE sz;
    int pad_x = 14;
    int gap = 28;
    int sc_w = 0;
    int lab_w = 0;

    if (!it) {
        mis->itemWidth = 160;
        mis->itemHeight = 28;
        return;
    }
    if (!it->label) {
        mis->itemWidth = 160;
        mis->itemHeight = 9;
        return;
    }

    hdc = GetDC(app->hwnd);
    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(hdc, it->label, (int)wcslen(it->label), &sz);
    lab_w = sz.cx;
    if (it->shortcut) {
        GetTextExtentPoint32W(hdc, it->shortcut, (int)wcslen(it->shortcut), &sz);
        sc_w = sz.cx;
    }
    SelectObject(hdc, oldf);
    ReleaseDC(app->hwnd, hdc);

    mis->itemWidth = (UINT)(pad_x + lab_w + (sc_w ? gap + sc_w : 0) + pad_x);
    mis->itemHeight = 28;
}

static void draw_file_menu_item(AppState *app, const DRAWITEMSTRUCT *dis)
{
    const FileMenuOd *it = (const FileMenuOd *)dis->itemData;
    COLORREF bg, text, muted, sep, hot, border;
    RECT rc = dis->rcItem;
    HDC hdc = dis->hDC;
    int selected = (dis->itemState & ODS_SELECTED) != 0;
    HBRUSH br;
    HFONT oldf;

    file_menu_colors(app, &bg, &text, &muted, &sep, &hot, &border);
    (void)border;

    if (!it || !it->label) {
        int mid = (rc.top + rc.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, sep);
        HPEN oldp = (HPEN)SelectObject(hdc, pen);
        br = CreateSolidBrush(bg);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        MoveToEx(hdc, rc.left + 10, mid, NULL);
        LineTo(hdc, rc.right - 10, mid);
        SelectObject(hdc, oldp);
        DeleteObject(pen);
        return;
    }

    br = CreateSolidBrush(selected ? hot : bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    oldf = (HFONT)SelectObject(hdc,
        app->font_ui ? app->font_ui : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    {
        RECT rlab = rc;
        rlab.left += 14;
        rlab.right -= 14;
        DrawTextW(hdc, it->label, -1, &rlab,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (it->shortcut) {
        RECT rsc = rc;
        rsc.left += 14;
        rsc.right -= 14;
        SetTextColor(hdc, muted);
        DrawTextW(hdc, it->shortcut, -1, &rsc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdc, oldf);
}

static void show_about(AppState *app)
{
    wchar_t buf[256];
    _snwprintf(buf, 256,
        L"DaBom %s\n\nWindows 네이티브 사진 뷰어\nWin32 + WIC",
        DABOM_VERSION_W);
    buf[255] = 0;
    MessageBoxW(app->hwnd, buf, L"프로그램 정보", MB_OK | MB_ICONINFORMATION);
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
        dabom_theme_apply(app, 1);
        create_menubar(app);
        app->status = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 100, DABOM_STATUS_H, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        app->status_meta = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            0, 0, 100, DABOM_STATUS_H, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(app->status, WM_SETFONT, (WPARAM)app->font_status, TRUE);
        SendMessageW(app->status_meta, WM_SETFONT, (WPARAM)app->font_status, TRUE);
        layout_children(app);
        dabom_update_chrome(app);
        return 0;

    case WM_SIZE:
        layout_children(app);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_SETTINGCHANGE:
        /* Apps dark/light toggle: lParam == L"ImmersiveColorSet". */
        if (lParam && lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet") == 0)
            dabom_theme_apply(app, 1);
        /* System UI fonts / metrics (SPI_SETNONCLIENTMETRICS, etc.). */
        dabom_create_fonts(app);
        dabom_apply_fonts(app);
        return 0;

    case WM_THEMECHANGED:
        dabom_theme_apply(app, 1);
        return 0;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
        if (mis && mis->CtlType == ODT_MENU) {
            measure_file_menu_item(app, mis);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lParam;
        if (dis && dis->CtlType == ODT_MENU) {
            draw_file_menu_item(app, dis);
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
        }
        return 0;

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_ESCAPE:
            if (app->mode == VIEW_FULL)
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
        case VK_SUBTRACT:
            dabom_zoom_by(app, 0.8);
            break;
        case '0':
            dabom_fit(app);
            break;
        case '1':
            dabom_actual_size(app);
            break;
        case 'O':
            if (GetKeyState(VK_CONTROL) < 0)
                dabom_open_folder_dialog(app);
            break;
        case VK_F1:
            show_about(app);
            break;
        case 'X':
        case 'x':
            if (GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0)
                DestroyWindow(hwnd);
            break;
        case VK_TAB:
            show_file_info(app);
            break;
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (app->mode == VIEW_GRID) {
            /* No selection focus in grid — keep vertical scroll. */
            app->scroll_y -= delta;
            if (app->scroll_y < 0) app->scroll_y = 0;
            InvalidateRect(hwnd, NULL, TRUE);
        } else if (app->mode == VIEW_FULL) {
            /* Plain wheel: next / previous (zoom stays on menu / +/- keys). */
            if (delta < 0)
                dabom_next(app, 1);   /* wheel down → next */
            else if (delta > 0)
                dabom_next(app, -1);  /* wheel up → previous */
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
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
        if (app->dragging && app->mode == VIEW_FULL) {
            app->pan_x += mx - app->drag_ox;
            app->pan_y += my - app->drag_oy;
            app->drag_ox = mx;
            app->drag_oy = my;
            app->fit_mode = 0;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (app->dragging) {
            app->dragging = 0;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (app->mode == VIEW_FULL)
            dabom_set_mode(app, VIEW_GRID);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_SLIDE && app->slideshow)
            dabom_next(app, 1);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, app->palette.text);
        SetBkColor(hdc, app->palette.surface);
        if (!app->br_status)
            app->br_status = CreateSolidBrush(app->palette.surface);
        return (LRESULT)app->br_status;
    }

    case WM_DESTROY:
        if (app->slideshow)
            KillTimer(hwnd, ID_TIMER_SLIDE);
        destroy_menubar(app);
        dabom_clear_files(app);
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
    int ok;

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

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (!ok) {
        /* Older layout without iPaddedBorderWidth */
        ncm.cbSize = sizeof(ncm) - sizeof(int);
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    }

    if (ok) {
        app->font_ui = CreateFontIndirectW(&ncm.lfMessageFont);
        app->font_btn = CreateFontIndirectW(&ncm.lfMessageFont);
        app->font_status = CreateFontIndirectW(&ncm.lfStatusFont);
        lf_brand = ncm.lfMessageFont;
        msg_h = lf_brand.lfHeight;
        if (msg_h == 0)
            msg_h = -12;
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
    if (app->status_meta)
        SendMessageW(app->status_meta, WM_SETFONT, (WPARAM)app->font_status, TRUE);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;

    (void)hPrev;
    (void)cmd;

    ZeroMemory(&g_app, sizeof(g_app));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
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
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"창 클래스 등록 실패", L"DaBom", MB_ICONERROR);
        dabom_wic_shutdown(&g_app);
        CoUninitialize();
        return 1;
    }

    g_app.mode = VIEW_EMPTY;

    hwnd = CreateWindowExW(
        0, L"DaBomPhotoViewer", DABOM_TITLE_W,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740,
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
