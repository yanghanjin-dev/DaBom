#include "dabom.h"

#ifndef MONITOR_DEFAULTTONEAREST
#define MONITOR_DEFAULTTONEAREST 2
#endif

static int has_current_image(AppState *app)
{
    return app && app->file_count > 0 && app->current >= 0
        && app->current < app->file_count && app->full_bmp != NULL;
}

static void ensure_full_loaded(AppState *app)
{
    if (!app || app->file_count <= 0)
        return;
    if (app->current < 0 || app->current >= app->file_count)
        app->current = 0;
    if (!app->full_bmp)
        dabom_load_current_full(app);
}

void dabom_toggle_fullscreen(AppState *app)
{
    HWND hwnd;
    if (!app || !(hwnd = app->hwnd))
        return;

    if (!app->fullscreen) {
        MONITORINFO mi;
        HMONITOR mon;

        ZeroMemory(&app->fs_placement, sizeof(app->fs_placement));
        app->fs_placement.length = sizeof(app->fs_placement);
        GetWindowPlacement(hwnd, &app->fs_placement);
        app->fs_style = GetWindowLongW(hwnd, GWL_STYLE);
        app->fs_menu = GetMenu(hwnd);
        if (app->fs_menu)
            SetMenu(hwnd, NULL);
        if (app->status)
            ShowWindow(app->status, SW_HIDE);

        mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);

        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        app->fullscreen = 1;
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, app->fs_style | WS_VISIBLE);
        if (app->fs_menu) {
            SetMenu(hwnd, app->fs_menu);
            app->fs_menu = NULL;
        }
        if (app->status)
            ShowWindow(app->status, SW_SHOW);
        SetWindowPlacement(hwnd, &app->fs_placement);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        app->fullscreen = 0;
        dabom_update_chrome(app);
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

void dabom_toggle_continuous(AppState *app)
{
    if (!app) return;
    app->continuous = !app->continuous;
    app->scroll_y = 0;
    if (app->continuous && app->file_count > 0) {
        ensure_full_loaded(app);
        if (app->mode != VIEW_FULL)
            dabom_set_mode(app, VIEW_FULL);
        dabom_load_page_bitmaps(app);
    }
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_toggle_smooth(AppState *app)
{
    if (!app) return;
    app->smooth = !app->smooth;
    dabom_save_settings(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_toggle_pixel_info(AppState *app)
{
    if (!app) return;
    app->pixel_info = !app->pixel_info;
    app->pix_valid = 0;
    if (!app->pixel_info)
        dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_toggle_hdr(AppState *app)
{
    if (!app) return;
    app->hdr_preview = !app->hdr_preview;
    InvalidateRect(app->hwnd, NULL, TRUE);
    dabom_set_status(app, app->hdr_preview
        ? L"HDR 미리보기 ON (밝기/대비 부스팅)"
        : L"HDR 미리보기 OFF");
}

static void apply_rotated(AppState *app, HBITMAP rotated, UINT w, UINT h)
{
    if (!rotated) {
        MessageBoxW(app->hwnd, L"회전에 실패했습니다.", L"회전",
            MB_OK | MB_ICONWARNING);
        return;
    }
    dabom_free_bitmap(&app->full_bmp);
    app->full_bmp = rotated;
    app->full_w = w;
    app->full_h = h;
    if (app->layout == LAYOUT_FIT || app->layout == LAYOUT_FIT_LARGE ||
        app->layout == LAYOUT_FIT_WIDTH || app->layout == LAYOUT_FIT_HEIGHT)
        app->fit_mode = 1;
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_rotate_current(AppState *app)
{
    HBITMAP rotated;
    UINT w, h;
    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"회전할 이미지가 없습니다.", L"회전하기",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    w = app->full_w;
    h = app->full_h;
    rotated = dabom_rotate_bitmap_90cw(app->full_bmp, &w, &h);
    apply_rotated(app, rotated, w, h);
}

void dabom_rotate_current_ccw(AppState *app)
{
    HBITMAP rotated;
    UINT w, h;
    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"회전할 이미지가 없습니다.", L"이미지 돌려보기",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    w = app->full_w;
    h = app->full_h;
    rotated = dabom_rotate_bitmap_90ccw(app->full_bmp, &w, &h);
    apply_rotated(app, rotated, w, h);
}

void dabom_copy_current(AppState *app)
{
    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"복사할 이미지가 없습니다.", L"클립보드",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!dabom_copy_bitmap_clipboard(app->hwnd, app->full_bmp, app->full_w, app->full_h))
        MessageBoxW(app->hwnd, L"클립보드 복사에 실패했습니다.", L"클립보드",
            MB_OK | MB_ICONWARNING);
    else
        dabom_set_status(app, L"클립보드에 복사됨");
}

void dabom_paste_clipboard(AppState *app)
{
    UINT w = 0, h = 0;
    HBITMAP bmp = dabom_bitmap_from_clipboard(app->hwnd, &w, &h);
    wchar_t temp_dir[MAX_PATH], temp_path[DABOM_MAX_PATH];

    if (!bmp) {
        MessageBoxW(app->hwnd, L"클립보드에 이미지가 없습니다.", L"붙여넣기",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    GetTempPathW(MAX_PATH, temp_dir);
    _snwprintf(temp_path, DABOM_MAX_PATH, L"%sDaBom_paste_%u.png",
        temp_dir, (unsigned)GetTickCount());
    temp_path[DABOM_MAX_PATH - 1] = 0;

    if (!dabom_save_bitmap(app, bmp, w, h, temp_path, &GUID_ContainerFormatPng)) {
        /* Keep in memory even if temp save fails. */
        dabom_clear_files(app);
        dabom_free_bitmap(&app->full_bmp);
        app->full_bmp = bmp;
        app->full_w = w;
        app->full_h = h;
        app->file_count = 0;
        app->current = 0;
        dabom_set_mode(app, VIEW_FULL);
        dabom_set_status(app, L"클립보드 이미지 (메모리)");
        InvalidateRect(app->hwnd, NULL, TRUE);
        return;
    }
    DeleteObject(bmp);

    dabom_clear_files(app);
    if (dabom_add_file(app, temp_path)) {
        app->current = 0;
        dabom_load_current_full(app);
        dabom_set_mode(app, VIEW_FULL);
        dabom_set_status(app, L"클립보드에서 붙여넣기");
    }
    InvalidateRect(app->hwnd, NULL, TRUE);
}

static int save_dialog(AppState *app, wchar_t *path_out, int cch, int convert_mode)
{
    OPENFILENAMEW ofn;
    wchar_t file[DABOM_MAX_PATH];
    const wchar_t *src_name;
    wchar_t def_name[MAX_PATH];

    ensure_full_loaded(app);
    if (!has_current_image(app))
        return 0;

    file[0] = 0;
    if (app->file_count > 0 && app->current >= 0 && app->current < app->file_count) {
        src_name = wcsrchr(app->files[app->current].path, L'\\');
        src_name = src_name ? src_name + 1 : app->files[app->current].path;
        wcsncpy(def_name, src_name, MAX_PATH - 1);
        def_name[MAX_PATH - 1] = 0;
        {
            wchar_t *dot = wcsrchr(def_name, L'.');
            if (dot) *dot = 0;
        }
    } else {
        wcscpy(def_name, L"image");
    }
    if (convert_mode)
        wcsncat(def_name, L".png", MAX_PATH - wcslen(def_name) - 1);
    else {
        const wchar_t *ext = NULL;
        if (app->file_count > 0)
            ext = wcsrchr(app->files[app->current].path, L'.');
        wcsncat(def_name, ext ? ext : L".png", MAX_PATH - wcslen(def_name) - 1);
    }
    wcsncpy(file, def_name, DABOM_MAX_PATH - 1);

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = DABOM_MAX_PATH;
    ofn.lpstrFilter =
        L"PNG (*.png)\0*.png\0"
        L"JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
        L"BMP (*.bmp)\0*.bmp\0"
        L"WebP (*.webp)\0*.webp\0"
        L"모든 파일 (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";
    if (app->last_folder[0])
        ofn.lpstrInitialDir = app->last_folder;

    if (!GetSaveFileNameW(&ofn))
        return 0;
    wcsncpy(path_out, file, cch - 1);
    path_out[cch - 1] = 0;
    return 1;
}

void dabom_save_as_current(AppState *app)
{
    wchar_t path[DABOM_MAX_PATH];
    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"저장할 이미지가 없습니다.", L"다른 이름으로 저장",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!save_dialog(app, path, DABOM_MAX_PATH, 0))
        return;
    if (!dabom_save_bitmap(app, app->full_bmp, app->full_w, app->full_h, path, NULL))
        MessageBoxW(app->hwnd, L"저장에 실패했습니다.", L"다른 이름으로 저장",
            MB_OK | MB_ICONWARNING);
    else
        dabom_set_status(app, L"저장 완료");
}

void dabom_convert_current(AppState *app)
{
    wchar_t path[DABOM_MAX_PATH];
    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"변환할 이미지가 없습니다.", L"포맷변환",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!save_dialog(app, path, DABOM_MAX_PATH, 1))
        return;
    if (!dabom_save_bitmap(app, app->full_bmp, app->full_w, app->full_h, path, NULL))
        MessageBoxW(app->hwnd, L"포맷 변환 저장에 실패했습니다.", L"포맷변환",
            MB_OK | MB_ICONWARNING);
    else
        dabom_set_status(app, L"포맷 변환 저장 완료");
}

/* ---- Resize dialog ---- */
typedef struct {
    AppState *app;
    HWND hwnd, ed_w, ed_h, chk_aspect;
    int keep_aspect;
    UINT orig_w, orig_h;
    double aspect;
} ResizeDlg;

static ResizeDlg g_resize;

static void resize_sync_from_width(void)
{
    wchar_t buf[32];
    int w;
    GetWindowTextW(g_resize.ed_w, buf, 32);
    w = _wtoi(buf);
    if (w < 1) w = 1;
    if (g_resize.keep_aspect && g_resize.aspect > 0) {
        int h = (int)((double)w / g_resize.aspect + 0.5);
        if (h < 1) h = 1;
        _snwprintf(buf, 32, L"%d", h);
        SetWindowTextW(g_resize.ed_h, buf);
    }
}

static void resize_sync_from_height(void)
{
    wchar_t buf[32];
    int h;
    GetWindowTextW(g_resize.ed_h, buf, 32);
    h = _wtoi(buf);
    if (h < 1) h = 1;
    if (g_resize.keep_aspect && g_resize.aspect > 0) {
        int w = (int)((double)h * g_resize.aspect + 0.5);
        if (w < 1) w = 1;
        _snwprintf(buf, 32, L"%d", w);
        SetWindowTextW(g_resize.ed_w, buf);
    }
}

static int resize_read_wh(UINT *ow, UINT *oh)
{
    wchar_t buf[32];
    int w, h;
    GetWindowTextW(g_resize.ed_w, buf, 32);
    w = _wtoi(buf);
    GetWindowTextW(g_resize.ed_h, buf, 32);
    h = _wtoi(buf);
    if (w < 1 || h < 1 || w > 30000 || h > 30000)
        return 0;
    *ow = (UINT)w;
    *oh = (UINT)h;
    return 1;
}

static void resize_apply_view(void)
{
    UINT nw, nh;
    HBITMAP nb;
    AppState *app = g_resize.app;
    if (!resize_read_wh(&nw, &nh)) {
        MessageBoxW(g_resize.hwnd, L"올바른 크기를 입력하세요.", L"크기변경",
            MB_OK | MB_ICONWARNING);
        return;
    }
    nb = dabom_resize_bitmap(app->full_bmp, app->full_w, app->full_h, nw, nh, app->smooth);
    if (!nb) {
        MessageBoxW(g_resize.hwnd, L"크기 변경에 실패했습니다.", L"크기변경",
            MB_OK | MB_ICONWARNING);
        return;
    }
    dabom_free_bitmap(&app->full_bmp);
    app->full_bmp = nb;
    app->full_w = nw;
    app->full_h = nh;
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

static void resize_save_as(void)
{
    UINT nw, nh;
    HBITMAP nb;
    wchar_t path[DABOM_MAX_PATH];
    AppState *app = g_resize.app;
    if (!resize_read_wh(&nw, &nh)) {
        MessageBoxW(g_resize.hwnd, L"올바른 크기를 입력하세요.", L"크기변경",
            MB_OK | MB_ICONWARNING);
        return;
    }
    nb = dabom_resize_bitmap(app->full_bmp, app->full_w, app->full_h, nw, nh, app->smooth);
    if (!nb) {
        MessageBoxW(g_resize.hwnd, L"크기 변경에 실패했습니다.", L"크기변경",
            MB_OK | MB_ICONWARNING);
        return;
    }
    if (!save_dialog(app, path, DABOM_MAX_PATH, 0)) {
        DeleteObject(nb);
        return;
    }
    if (!dabom_save_bitmap(app, nb, nw, nh, path, NULL))
        MessageBoxW(g_resize.hwnd, L"저장에 실패했습니다.", L"크기변경",
            MB_OK | MB_ICONWARNING);
    else
        MessageBoxW(g_resize.hwnd, L"저장했습니다.", L"크기변경",
            MB_OK | MB_ICONINFORMATION);
    DeleteObject(nb);
}

static LRESULT CALLBACK ResizeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1001:
            if (HIWORD(wParam) == EN_KILLFOCUS && g_resize.keep_aspect)
                resize_sync_from_width();
            break;
        case 1002:
            if (HIWORD(wParam) == EN_KILLFOCUS && g_resize.keep_aspect)
                resize_sync_from_height();
            break;
        case 1003:
            g_resize.keep_aspect =
                (SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (g_resize.keep_aspect)
                resize_sync_from_width();
            break;
        case 1004: resize_apply_view(); break;
        case 1005: resize_save_as(); break;
        case 1006:
        case IDCANCEL: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        EnableWindow(g_resize.app->hwnd, TRUE);
        SetActiveWindow(g_resize.app->hwnd);
        g_resize.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void dabom_resize_dialog(AppState *app)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    wchar_t buf[32];
    static ATOM atom = 0;

    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"크기 변경할 이미지가 없습니다.", L"크기변경",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_resize.hwnd) {
        SetForegroundWindow(g_resize.hwnd);
        return;
    }

    ZeroMemory(&g_resize, sizeof(g_resize));
    g_resize.app = app;
    g_resize.orig_w = app->full_w;
    g_resize.orig_h = app->full_h;
    g_resize.aspect = app->full_h > 0 ? (double)app->full_w / (double)app->full_h : 1.0;
    g_resize.keep_aspect = 1;

    if (!atom) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ResizeWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"DaBomResizeDlg";
        atom = RegisterClassExW(&wc);
    }

    hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DaBomResizeDlg", L"크기변경",
        WS_CAPTION | WS_POPUP | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 220,
        app->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd) return;
    g_resize.hwnd = hwnd;

    CreateWindowW(L"STATIC", L"너비:", WS_CHILD | WS_VISIBLE, 20, 24, 50, 20, hwnd, NULL, NULL, NULL);
    g_resize.ed_w = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 80, 20, 100, 24, hwnd, (HMENU)1001, NULL, NULL);
    CreateWindowW(L"STATIC", L"높이:", WS_CHILD | WS_VISIBLE, 20, 58, 50, 20, hwnd, NULL, NULL, NULL);
    g_resize.ed_h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 80, 54, 100, 24, hwnd, (HMENU)1002, NULL, NULL);
    g_resize.chk_aspect = CreateWindowW(L"BUTTON", L"비율 유지",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 92, 120, 22, hwnd, (HMENU)1003, NULL, NULL);
    SendMessageW(g_resize.chk_aspect, BM_SETCHECK, BST_CHECKED, 0);
    CreateWindowW(L"BUTTON", L"보기에 적용", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 130, 100, 28, hwnd, (HMENU)1004, NULL, NULL);
    CreateWindowW(L"BUTTON", L"다른 이름으로 저장", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, 130, 140, 28, hwnd, (HMENU)1005, NULL, NULL);
    CreateWindowW(L"BUTTON", L"닫기", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        280, 130, 50, 28, hwnd, (HMENU)1006, NULL, NULL);

    _snwprintf(buf, 32, L"%u", app->full_w);
    SetWindowTextW(g_resize.ed_w, buf);
    _snwprintf(buf, 32, L"%u", app->full_h);
    SetWindowTextW(g_resize.ed_h, buf);

    EnableWindow(app->hwnd, FALSE);
    ShowWindow(hwnd, SW_SHOW);
}

void dabom_print_current(AppState *app)
{
    PRINTDLGW pd;
    DOCINFOW di;
    HDC hdc;
    BITMAP bm;
    BYTE *bits = NULL;
    BITMAPINFO bmi;
    int page_w, page_h, dw, dh;
    double sx, sy, scale;

    ensure_full_loaded(app);
    if (!has_current_image(app)) {
        MessageBoxW(app->hwnd, L"인쇄할 이미지가 없습니다.", L"인쇄하기",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = app->hwnd;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION;
    if (!PrintDlgW(&pd))
        return;
    hdc = pd.hDC;
    if (!hdc) goto cleanup;

    ZeroMemory(&bm, sizeof(bm));
    GetObjectW(app->full_bmp, sizeof(bm), &bm);
    bits = (BYTE *)bm.bmBits;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)app->full_w;
    bmi.bmiHeader.biHeight = -(LONG)app->full_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    page_w = GetDeviceCaps(hdc, HORZRES);
    page_h = GetDeviceCaps(hdc, VERTRES);
    sx = (double)page_w / (double)app->full_w;
    sy = (double)page_h / (double)app->full_h;
    scale = sx < sy ? sx : sy;
    dw = (int)(app->full_w * scale);
    dh = (int)(app->full_h * scale);

    ZeroMemory(&di, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = L"DaBom";
    if (StartDocW(hdc, &di) > 0) {
        if (StartPage(hdc) > 0) {
            if (bits) {
                StretchDIBits(hdc, (page_w - dw) / 2, (page_h - dh) / 2, dw, dh,
                    0, 0, (int)app->full_w, (int)app->full_h,
                    bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
            } else {
                HDC mem = CreateCompatibleDC(hdc);
                HBITMAP old = (HBITMAP)SelectObject(mem, app->full_bmp);
                SetStretchBltMode(hdc, HALFTONE);
                StretchBlt(hdc, (page_w - dw) / 2, (page_h - dh) / 2, dw, dh,
                    mem, 0, 0, (int)app->full_w, (int)app->full_h, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
            EndPage(hdc);
        }
        EndDoc(hdc);
    }
    DeleteDC(hdc);
cleanup:
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
}

void dabom_show_camera_info(AppState *app)
{
    wchar_t info[2048];
    const wchar_t *path;
    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"열린 이미지가 없습니다.", L"카메라 정보",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    info[0] = 0;
    if (!dabom_read_camera_info(app, path, info, 2048) || !info[0])
        MessageBoxW(app->hwnd, L"없음", L"카메라 정보", MB_OK | MB_ICONINFORMATION);
    else
        MessageBoxW(app->hwnd, info, L"카메라 정보", MB_OK | MB_ICONINFORMATION);
}

void dabom_show_file_props(AppState *app)
{
    const wchar_t *path;
    SHELLEXECUTEINFOW sei;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    wchar_t buf[1024];
    SYSTEMTIME st;
    FILETIME local;
    UINT w, h;

    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"열린 이미지가 없습니다.", L"파일 속성",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = app->hwnd;
    sei.lpVerb = L"properties";
    sei.lpFile = path;
    sei.nShow = SW_SHOW;
    if (ShellExecuteExW(&sei))
        return;

    ensure_full_loaded(app);
    w = app->full_w;
    h = app->full_h;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER ul;
        ul.LowPart = fad.nFileSizeLow;
        ul.HighPart = fad.nFileSizeHigh;
        FileTimeToLocalFileTime(&fad.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
        _snwprintf(buf, 1024,
            L"경로: %s\n크기: %llu 바이트\n수정: %04u-%02u-%02u %02u:%02u:%02u\n해상도: %ux%u",
            path, (unsigned long long)ul.QuadPart,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, w, h);
    } else {
        _snwprintf(buf, 1024, L"경로: %s\n해상도: %ux%u", path, w, h);
    }
    buf[1023] = 0;
    MessageBoxW(app->hwnd, buf, L"파일 속성", MB_OK | MB_ICONINFORMATION);
}

void dabom_delete_current(AppState *app)
{
    const wchar_t *path;
    wchar_t msg[DABOM_MAX_PATH + 64];
    int idx;

    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"삭제할 파일이 없습니다.", L"삭제",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    _snwprintf(msg, DABOM_MAX_PATH + 64, L"이 파일을 삭제할까요?\n\n%s", path);
    msg[DABOM_MAX_PATH + 63] = 0;
    if (MessageBoxW(app->hwnd, msg, L"파일 삭제", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    idx = app->current;
    if (!DeleteFileW(path)) {
        MessageBoxW(app->hwnd, L"파일 삭제에 실패했습니다.", L"삭제",
            MB_OK | MB_ICONWARNING);
        return;
    }
    dabom_remove_file_at(app, idx);
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

void dabom_open_explorer(AppState *app)
{
    wchar_t params[DABOM_MAX_PATH + 32];
    const wchar_t *path;
    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        if (app->last_folder[0])
            ShellExecuteW(app->hwnd, L"explore", app->last_folder, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    path = app->files[app->current].path;
    _snwprintf(params, DABOM_MAX_PATH + 32, L"/select,\"%s\"", path);
    params[DABOM_MAX_PATH + 31] = 0;
    ShellExecuteW(app->hwnd, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
}

void dabom_open_edit_app(AppState *app)
{
    const wchar_t *path;
    HINSTANCE r;
    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"편집할 파일이 없습니다.", L"편집 프로그램",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    r = ShellExecuteW(app->hwnd, L"edit", path, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        ShellExecuteW(app->hwnd, L"open", L"mspaint.exe", path, NULL, SW_SHOWNORMAL);
}

void dabom_copy_to_library(AppState *app)
{
    wchar_t pictures[MAX_PATH], dest_dir[DABOM_MAX_PATH], dest[DABOM_MAX_PATH];
    const wchar_t *path, *name;

    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"복사할 파일이 없습니다.", L"사진 보관함",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, pictures))) {
        MessageBoxW(app->hwnd, L"Pictures 폴더를 찾을 수 없습니다.", L"사진 보관함",
            MB_OK | MB_ICONWARNING);
        return;
    }
    _snwprintf(dest_dir, DABOM_MAX_PATH, L"%s\\DaBom", pictures);
    dest_dir[DABOM_MAX_PATH - 1] = 0;
    CreateDirectoryW(dest_dir, NULL);
    _snwprintf(dest, DABOM_MAX_PATH, L"%s\\%s", dest_dir, name);
    dest[DABOM_MAX_PATH - 1] = 0;
    if (!CopyFileW(path, dest, FALSE))
        MessageBoxW(app->hwnd, L"보관함 복사에 실패했습니다.", L"사진 보관함",
            MB_OK | MB_ICONWARNING);
    else
        dabom_set_status(app, L"사진 보관함으로 복사됨");
}

void dabom_set_wallpaper(AppState *app)
{
    const wchar_t *path;
    wchar_t bmp_path[DABOM_MAX_PATH];
    wchar_t temp_dir[MAX_PATH];

    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"배경으로 지정할 이미지가 없습니다.", L"바탕 화면",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    /* SPI_SETDESKWALLPAPER prefers BMP on some systems — write temp BMP via WIC. */
    ensure_full_loaded(app);
    GetTempPathW(MAX_PATH, temp_dir);
    _snwprintf(bmp_path, DABOM_MAX_PATH, L"%sDaBom_wallpaper.bmp", temp_dir);
    bmp_path[DABOM_MAX_PATH - 1] = 0;
    if (has_current_image(app)) {
        dabom_save_bitmap(app, app->full_bmp, app->full_w, app->full_h,
            bmp_path, &GUID_ContainerFormatBmp);
        path = bmp_path;
    }
    if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path,
            SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        MessageBoxW(app->hwnd, L"바탕 화면 설정에 실패했습니다.", L"바탕 화면",
            MB_OK | MB_ICONWARNING);
    else
        dabom_set_status(app, L"바탕 화면 배경으로 지정됨");
}

void dabom_bookmark_current(AppState *app)
{
    const wchar_t *path;
    if (!app || app->file_count <= 0 || app->current < 0 || app->current >= app->file_count) {
        MessageBoxW(app->hwnd, L"북마크할 파일이 없습니다.", L"북마크",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    path = app->files[app->current].path;
    if (!dabom_bookmark_add(app, path))
        MessageBoxW(app->hwnd, L"북마크 한도에 도달했습니다.", L"북마크",
            MB_OK | MB_ICONWARNING);
    else
        dabom_set_status(app, L"북마크에 추가됨");
}

void dabom_bookmark_jump(AppState *app, int index)
{
    int i;
    if (!app || index < 0 || index >= app->bookmark_count)
        return;
    /* If already in list, jump; else open as single file. */
    for (i = 0; i < app->file_count; i++) {
        if (_wcsicmp(app->files[i].path, app->bookmarks[index]) == 0) {
            dabom_goto_index(app, i);
            return;
        }
    }
    dabom_clear_files(app);
    if (dabom_add_file(app, app->bookmarks[index])) {
        app->current = 0;
        dabom_load_current_full(app);
        dabom_set_mode(app, VIEW_FULL);
    } else {
        MessageBoxW(app->hwnd, L"북마크 파일을 열 수 없습니다.", L"북마크",
            MB_OK | MB_ICONWARNING);
    }
}

int dabom_map_view_to_image(AppState *app, int mx, int my, int *ix, int *iy)
{
    RECT rc;
    int cw, ch, dw, dh, dx, dy;
    double scale, zw, zh;

    if (!app || !app->full_bmp || app->full_w == 0 || app->full_h == 0)
        return 0;
    if (app->mode != VIEW_FULL || app->continuous || app->page_mode != PAGE_ONE)
        return 0;

    dabom_get_content_rect(app, &rc);
    cw = rc.right - rc.left;
    ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0)
        return 0;

    zw = (double)cw / (double)app->full_w;
    zh = (double)ch / (double)app->full_h;

    if (app->layout == LAYOUT_STRETCH) {
        dw = cw; dh = ch; dx = rc.left; dy = rc.top;
        if (mx < dx || my < dy || mx >= dx + dw || my >= dy + dh) return 0;
        *ix = (int)((double)(mx - dx) * (double)app->full_w / (double)dw);
        *iy = (int)((double)(my - dy) * (double)app->full_h / (double)dh);
    } else {
        scale = app->zoom;
        if (app->fit_mode || app->layout == LAYOUT_FIT || app->layout == LAYOUT_FIT_LARGE ||
            app->layout == LAYOUT_FIT_WIDTH || app->layout == LAYOUT_FIT_HEIGHT) {
            if (app->layout == LAYOUT_FIT_WIDTH) scale = zw;
            else if (app->layout == LAYOUT_FIT_HEIGHT) scale = zh;
            else if (app->layout == LAYOUT_FIT_LARGE) {
                scale = (zw < zh ? zw : zh);
                if (scale > 1.0) scale = 1.0;
            } else
                scale = zw < zh ? zw : zh;
            if (scale <= 0) scale = 1.0;
        }
        dw = (int)(app->full_w * scale + 0.5);
        dh = (int)(app->full_h * scale + 0.5);
        dx = rc.left + (cw - dw) / 2 + app->pan_x;
        dy = rc.top + (ch - dh) / 2 + app->pan_y;
        if (mx < dx || my < dy || mx >= dx + dw || my >= dy + dh) return 0;
        *ix = (int)((double)(mx - dx) / scale);
        *iy = (int)((double)(my - dy) / scale);
    }
    if (*ix < 0) *ix = 0;
    if (*iy < 0) *iy = 0;
    if ((UINT)*ix >= app->full_w) *ix = (int)app->full_w - 1;
    if ((UINT)*iy >= app->full_h) *iy = (int)app->full_h - 1;
    return 1;
}

void dabom_sample_pixel_at(AppState *app, int mx, int my)
{
    int ix, iy, r, g, b;
    wchar_t meta[256];
    app->pix_valid = 0;
    if (!app->pixel_info) return;
    if (!dabom_map_view_to_image(app, mx, my, &ix, &iy)) return;
    if (!dabom_get_pixel_rgb(app->full_bmp, app->full_w, app->full_h, ix, iy, &r, &g, &b))
        return;
    app->pix_x = ix; app->pix_y = iy;
    app->pix_r = r; app->pix_g = g; app->pix_b = b;
    app->pix_valid = 1;
    _snwprintf(meta, 256, L"픽셀 (%d, %d)  RGB(%d, %d, %d)", ix, iy, r, g, b);
    meta[255] = 0;
    wcsncpy(app->status_right, meta, 255);
    app->status_right[255] = 0;
    if (app->status)
        SendMessageW(app->status, SB_SETTEXTW, 1 | SBT_OWNERDRAW, (LPARAM)app->status_right);
    InvalidateRect(app->hwnd, NULL, FALSE);
}

int dabom_handle_ctx_command(AppState *app, UINT id)
{
    if (id >= ID_CTX_BOOKMARK_BASE && id < ID_CTX_BOOKMARK_BASE + DABOM_MAX_BOOKMARKS) {
        dabom_bookmark_jump(app, (int)(id - ID_CTX_BOOKMARK_BASE));
        return 1;
    }
    switch (id) {
    case ID_CTX_FULLSCREEN: dabom_toggle_fullscreen(app); return 1;
    case ID_CTX_CONTINUOUS: dabom_toggle_continuous(app); return 1;
    case ID_CTX_SMOOTH: dabom_toggle_smooth(app); return 1;
    case ID_CTX_LAYOUT_FIT_LARGE: dabom_set_layout(app, LAYOUT_FIT_LARGE); return 1;
    case ID_CTX_LAYOUT_FIT: dabom_set_layout(app, LAYOUT_FIT); return 1;
    case ID_CTX_LAYOUT_WIDTH: dabom_set_layout(app, LAYOUT_FIT_WIDTH); return 1;
    case ID_CTX_LAYOUT_HEIGHT: dabom_set_layout(app, LAYOUT_FIT_HEIGHT); return 1;
    case ID_CTX_LAYOUT_ACTUAL_KEEP: dabom_set_layout(app, LAYOUT_ACTUAL_KEEP); return 1;
    case ID_CTX_LAYOUT_ZOOM_KEEP: dabom_set_layout(app, LAYOUT_ZOOM_KEEP); return 1;
    case ID_CTX_LAYOUT_STRETCH: dabom_set_layout(app, LAYOUT_STRETCH); return 1;
    case ID_CTX_LAYOUT_ACTUAL: dabom_set_layout(app, LAYOUT_ACTUAL); return 1;
    case ID_CTX_PAGE_ONE: dabom_set_page_mode(app, PAGE_ONE); return 1;
    case ID_CTX_PAGE_TWO: dabom_set_page_mode(app, PAGE_TWO); return 1;
    case ID_CTX_PAGE_FOUR: dabom_set_page_mode(app, PAGE_FOUR); return 1;
    case ID_CTX_COPY: dabom_copy_current(app); return 1;
    case ID_CTX_SAVE_AS: dabom_save_as_current(app); return 1;
    case ID_CTX_ROTATE:
    case ID_CTX_IMG_ROTATE: dabom_rotate_current(app); return 1;
    case ID_CTX_ROTATE_CCW: dabom_rotate_current_ccw(app); return 1;
    case ID_CTX_RESIZE:
    case ID_CTX_IMG_RESIZE: dabom_resize_dialog(app); return 1;
    case ID_CTX_CONVERT:
    case ID_CTX_IMG_CONVERT: dabom_convert_current(app); return 1;
    case ID_CTX_PRINT: dabom_print_current(app); return 1;
    case ID_CTX_PIXEL: dabom_toggle_pixel_info(app); return 1;
    case ID_CTX_CAMERA: dabom_show_camera_info(app); return 1;
    case ID_CTX_PROPS: dabom_show_file_props(app); return 1;
    case ID_CTX_DELETE: dabom_delete_current(app); return 1;
    case ID_CTX_SORT_NAME: dabom_set_sort_mode(app, SORT_NAME); return 1;
    case ID_CTX_SORT_DATE: dabom_set_sort_mode(app, SORT_DATE); return 1;
    case ID_CTX_SORT_SIZE: dabom_set_sort_mode(app, SORT_SIZE); return 1;
    case ID_CTX_HDR: dabom_toggle_hdr(app); return 1;
    case ID_CTX_BOOKMARK_ADD: dabom_bookmark_current(app); return 1;
    case ID_CTX_PASTE: dabom_paste_clipboard(app); return 1;
    case ID_CTX_EXPLORER: dabom_open_explorer(app); return 1;
    case ID_CTX_EDIT_APP: dabom_open_edit_app(app); return 1;
    case ID_CTX_LIBRARY: dabom_copy_to_library(app); return 1;
    case ID_CTX_WALLPAPER: dabom_set_wallpaper(app); return 1;
    case ID_MENU_OPEN_FOLDER: dabom_open_folder_dialog(app); return 1;
    default: return 0;
    }
}
