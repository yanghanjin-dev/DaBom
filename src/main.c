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

    if (app->hwnd) {
        InvalidateRect(app->hwnd, NULL, TRUE);
        if (app->status)
            InvalidateRect(app->status, NULL, TRUE);
    }
}

static void layout_children(AppState *app)
{
    RECT rc;
    GetClientRect(app->hwnd, &rc);
    if (app->status)
        MoveWindow(app->status, 0, rc.bottom - DABOM_STATUS_H, rc.right, DABOM_STATUS_H, TRUE);
}

void dabom_set_status(AppState *app, const wchar_t *text)
{
    if (app->status)
        SetWindowTextW(app->status, text ? text : L"");
}

void dabom_update_chrome(AppState *app)
{
    wchar_t buf[DABOM_MAX_PATH + 64];
    int enable = app->file_count > 0;

    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_PREV), enable);
    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_NEXT), enable);
    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_ZOOM_IN), app->mode == VIEW_FULL);
    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_ZOOM_OUT), app->mode == VIEW_FULL);
    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_FIT), app->mode == VIEW_FULL);
    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_ACTUAL), app->mode == VIEW_FULL);
    EnableWindow(GetDlgItem(app->toolbar, ID_BTN_SLIDE), enable);
    SetWindowTextW(GetDlgItem(app->toolbar, ID_BTN_SLIDE),
        app->slideshow ? L"슬라이드 중지" : L"슬라이드쇼");

    if (app->mode == VIEW_EMPTY || app->file_count == 0) {
        dabom_set_status(app, L"폴더 또는 이미지를 열어 DaBom으로 사진을 보세요");
        SetWindowTextW(app->hwnd, DABOM_TITLE_W);
    } else if (app->mode == VIEW_FULL && app->current >= 0 && app->current < app->file_count) {
        const wchar_t *name = wcsrchr(app->files[app->current].path, L'\\');
        name = name ? name + 1 : app->files[app->current].path;
        _snwprintf(buf, DABOM_MAX_PATH + 64, L"%s  (%d / %d)%s",
            name, app->current + 1, app->file_count,
            app->slideshow ? L"  · 슬라이드쇼" : L"");
        buf[DABOM_MAX_PATH + 63] = 0;
        dabom_set_status(app, buf);
        SetWindowTextW(app->hwnd, buf);
    } else {
        _snwprintf(buf, DABOM_MAX_PATH + 64, L"DaBom — 이미지 %d장", app->file_count);
        buf[DABOM_MAX_PATH + 63] = 0;
        dabom_set_status(app, buf);
        SetWindowTextW(app->hwnd, DABOM_TITLE_W);
    }
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
    rc.top += DABOM_TOOLBAR_H;
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
    GetClientRect(app->hwnd, rc);
    rc->top += DABOM_TOOLBAR_H;
    rc->bottom -= DABOM_STATUS_H;
}

static void paint_empty(AppState *app, HDC hdc, const RECT *rc)
{
    RECT r = *rc;
    const wchar_t *brand = L"DaBom";
    const wchar_t *ver = DABOM_TITLE_W; /* e.g. DaBom 0.0.2 */
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

    /* toolbar strip */
    {
        RECT tr = { 0, 0, w, DABOM_TOOLBAR_H };
        HBRUSH br = CreateSolidBrush(app->palette.surface);
        FillRect(mem, &tr, br);
        DeleteObject(br);
        /* accent underline */
        {
            HPEN pen = CreatePen(PS_SOLID, 2, app->palette.accent);
            HPEN op = (HPEN)SelectObject(mem, pen);
            MoveToEx(mem, 0, DABOM_TOOLBAR_H - 1, NULL);
            LineTo(mem, w, DABOM_TOOLBAR_H - 1);
            SelectObject(mem, op);
            DeleteObject(pen);
        }
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

static void create_toolbar(AppState *app)
{
    int i;
    struct { int id; const wchar_t *label; int w; } buttons[] = {
        { ID_BTN_FOLDER,   L"폴더 열기", 90 },
        { ID_BTN_FILES,    L"파일 열기", 90 },
        { ID_BTN_PREV,     L"이전", 64 },
        { ID_BTN_NEXT,     L"다음", 64 },
        { ID_BTN_ZOOM_OUT, L"축소", 56 },
        { ID_BTN_ZOOM_IN,  L"확대", 56 },
        { ID_BTN_FIT,      L"맞춤", 56 },
        { ID_BTN_ACTUAL,   L"원본크기", 80 },
        { ID_BTN_SLIDE,    L"슬라이드쇼", 100 },
        { ID_BTN_FONT_DEC, L"글자-", 52 },
        { ID_BTN_FONT_INC, L"글자+", 52 },
    };
    int x = 10;
    app->toolbar = app->hwnd; /* buttons are direct children; used with GetDlgItem */

    for (i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); i++) {
        HWND btn = CreateWindowExW(0, L"BUTTON", buttons[i].label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, 10, buttons[i].w, 28, app->hwnd, (HMENU)(INT_PTR)buttons[i].id,
            GetModuleHandleW(NULL), NULL);
        SendMessageW(btn, WM_SETFONT, (WPARAM)app->font_btn, TRUE);
        x += buttons[i].w + 6;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppState *app = &g_app;

    switch (msg) {
    case WM_CREATE:
        app->hwnd = hwnd;
        dabom_theme_apply(app, 1);
        create_toolbar(app);
        app->status = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 100, DABOM_STATUS_H, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(app->status, WM_SETFONT, (WPARAM)app->font_ui, TRUE);
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
        return 0;

    case WM_THEMECHANGED:
        dabom_theme_apply(app, 1);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_all(app);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_FOLDER: dabom_open_folder_dialog(app); break;
        case ID_BTN_FILES:  dabom_open_files_dialog(app); break;
        case ID_BTN_PREV:   dabom_next(app, -1); break;
        case ID_BTN_NEXT:   dabom_next(app, 1); break;
        case ID_BTN_ZOOM_IN:  dabom_zoom_by(app, 1.25); break;
        case ID_BTN_ZOOM_OUT: dabom_zoom_by(app, 0.8); break;
        case ID_BTN_FIT:    dabom_fit(app); break;
        case ID_BTN_ACTUAL: dabom_actual_size(app); break;
        case ID_BTN_SLIDE:  dabom_toggle_slideshow(app); break;
        case ID_BTN_FONT_DEC: dabom_set_ui_font_pt(app, app->ui_font_size_pt - 1); break;
        case ID_BTN_FONT_INC: dabom_set_ui_font_pt(app, app->ui_font_size_pt + 1); break;
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
            /* Plain wheel: next / previous (zoom stays on toolbar / +/- keys). */
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

    case WM_MOUSEMOVE:
        if (app->dragging && app->mode == VIEW_FULL) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            app->pan_x += mx - app->drag_ox;
            app->pan_y += my - app->drag_oy;
            app->drag_ox = mx;
            app->drag_oy = my;
            app->fit_mode = 0;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

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

/* True typographic points → GDI lfHeight (negative = character height). */
static int pt_to_lfheight(HDC hdc, int points)
{
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    if (dpi <= 0)
        dpi = 96;
    return -MulDiv(points, dpi, 72);
}

void dabom_create_fonts(AppState *app)
{
    HDC hdc = GetDC(NULL);
    int ui_pt = app->ui_font_size_pt;
    int brand_pt;
    int h_ui, h_btn, h_brand;

    if (ui_pt < DABOM_UI_FONT_PT_MIN)
        ui_pt = DABOM_UI_FONT_PT_DEFAULT;
    if (ui_pt > DABOM_UI_FONT_PT_MAX)
        ui_pt = DABOM_UI_FONT_PT_MAX;
    app->ui_font_size_pt = ui_pt;

    /* Brand hero ≈ 3.5× UI pt so it scales with the independent setting. */
    brand_pt = (ui_pt * DABOM_BRAND_FONT_PT_MULT) / DABOM_BRAND_FONT_PT_DIV;
    if (brand_pt < ui_pt * 3)
        brand_pt = ui_pt * 3;

    h_ui = pt_to_lfheight(hdc, ui_pt);
    h_btn = pt_to_lfheight(hdc, ui_pt); /* same ballpark as Agents UI Font Size feel */
    h_brand = pt_to_lfheight(hdc, brand_pt);
    ReleaseDC(NULL, hdc);

    if (app->font_ui) {
        DeleteObject(app->font_ui);
        app->font_ui = NULL;
    }
    if (app->font_btn) {
        DeleteObject(app->font_btn);
        app->font_btn = NULL;
    }
    if (app->font_brand) {
        DeleteObject(app->font_brand);
        app->font_brand = NULL;
    }

    app->font_ui = CreateFontW(
        h_ui, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    app->font_btn = CreateFontW(
        h_btn, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    app->font_brand = CreateFontW(
        h_brand, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Semibold");
}

void dabom_apply_fonts(AppState *app)
{
    static const int btn_ids[] = {
        ID_BTN_FOLDER, ID_BTN_FILES, ID_BTN_PREV, ID_BTN_NEXT,
        ID_BTN_ZOOM_OUT, ID_BTN_ZOOM_IN, ID_BTN_FIT, ID_BTN_ACTUAL,
        ID_BTN_SLIDE, ID_BTN_FONT_DEC, ID_BTN_FONT_INC
    };
    int i;

    if (!app->hwnd)
        return;
    for (i = 0; i < (int)(sizeof(btn_ids) / sizeof(btn_ids[0])); i++) {
        HWND btn = GetDlgItem(app->hwnd, btn_ids[i]);
        if (btn)
            SendMessageW(btn, WM_SETFONT, (WPARAM)app->font_btn, TRUE);
    }
    if (app->status)
        SendMessageW(app->status, WM_SETFONT, (WPARAM)app->font_ui, TRUE);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_set_ui_font_pt(AppState *app, int pt)
{
    if (pt < DABOM_UI_FONT_PT_MIN)
        pt = DABOM_UI_FONT_PT_MIN;
    if (pt > DABOM_UI_FONT_PT_MAX)
        pt = DABOM_UI_FONT_PT_MAX;
    if (pt == app->ui_font_size_pt)
        return;
    app->ui_font_size_pt = pt;
    dabom_create_fonts(app);
    dabom_apply_fonts(app);
    dabom_save_settings(app);
    {
        wchar_t buf[64];
        _snwprintf(buf, 64, L"UI 글자크기 %d pt", app->ui_font_size_pt);
        buf[63] = 0;
        dabom_set_status(app, buf);
    }
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
    g_app.ui_font_size_pt = DABOM_UI_FONT_PT_DEFAULT;
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
    if (g_app.font_btn) DeleteObject(g_app.font_btn);
    if (g_app.font_brand) DeleteObject(g_app.font_brand);
    if (g_app.br_status) DeleteObject(g_app.br_status);
    CoUninitialize();
    return (int)msg.wParam;
}
