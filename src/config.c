#include "dabom.h"

void dabom_config_path(AppState *app)
{
    wchar_t appdata[MAX_PATH];
    wchar_t dir[DABOM_MAX_PATH];

    app->config_path[0] = 0;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata)))
        return;

    _snwprintf(dir, DABOM_MAX_PATH, L"%s\\DaBom", appdata);
    dir[DABOM_MAX_PATH - 1] = 0;
    CreateDirectoryW(dir, NULL);

    _snwprintf(app->config_path, DABOM_MAX_PATH, L"%s\\settings.ini", dir);
    app->config_path[DABOM_MAX_PATH - 1] = 0;
}

static LayoutMode layout_from_str(const wchar_t *s)
{
    if (_wcsicmp(s, L"fit_large") == 0) return LAYOUT_FIT_LARGE;
    if (_wcsicmp(s, L"fit_width") == 0) return LAYOUT_FIT_WIDTH;
    if (_wcsicmp(s, L"fit_height") == 0) return LAYOUT_FIT_HEIGHT;
    if (_wcsicmp(s, L"actual_keep") == 0) return LAYOUT_ACTUAL_KEEP;
    if (_wcsicmp(s, L"zoom_keep") == 0) return LAYOUT_ZOOM_KEEP;
    if (_wcsicmp(s, L"stretch") == 0) return LAYOUT_STRETCH;
    if (_wcsicmp(s, L"actual") == 0) return LAYOUT_ACTUAL;
    return LAYOUT_FIT;
}

static const wchar_t *layout_to_str(LayoutMode m)
{
    switch (m) {
    case LAYOUT_FIT_LARGE: return L"fit_large";
    case LAYOUT_FIT_WIDTH: return L"fit_width";
    case LAYOUT_FIT_HEIGHT: return L"fit_height";
    case LAYOUT_ACTUAL_KEEP: return L"actual_keep";
    case LAYOUT_ZOOM_KEEP: return L"zoom_keep";
    case LAYOUT_STRETCH: return L"stretch";
    case LAYOUT_ACTUAL: return L"actual";
    default: return L"fit";
    }
}

void dabom_load_settings(AppState *app)
{
    int i, count, page;
    wchar_t key[32];
    wchar_t sort[32];
    wchar_t layout[32];

    if (!app->config_path[0])
        return;
    GetPrivateProfileStringW(
        L"DaBom", L"LastFolder", L"",
        app->last_folder, DABOM_MAX_PATH, app->config_path);

    GetPrivateProfileStringW(L"DaBom", L"SortMode", L"name", sort, 32, app->config_path);
    if (_wcsicmp(sort, L"date") == 0)
        app->sort_mode = SORT_DATE;
    else if (_wcsicmp(sort, L"size") == 0)
        app->sort_mode = SORT_SIZE;
    else
        app->sort_mode = SORT_NAME;

    app->smooth = GetPrivateProfileIntW(L"DaBom", L"Smooth", 1, app->config_path);

    GetPrivateProfileStringW(L"DaBom", L"Layout", L"fit", layout, 32, app->config_path);
    app->layout = layout_from_str(layout);
    app->fit_mode = (app->layout == LAYOUT_ACTUAL || app->layout == LAYOUT_ACTUAL_KEEP ||
                     app->layout == LAYOUT_ZOOM_KEEP) ? 0 : 1;

    page = GetPrivateProfileIntW(L"DaBom", L"PageMode", 1, app->config_path);
    if (page == 2) app->page_mode = PAGE_TWO;
    else if (page == 4) app->page_mode = PAGE_FOUR;
    else app->page_mode = PAGE_ONE;

    count = GetPrivateProfileIntW(L"Bookmarks", L"Count", 0, app->config_path);
    if (count < 0) count = 0;
    if (count > DABOM_MAX_BOOKMARKS) count = DABOM_MAX_BOOKMARKS;
    app->bookmark_count = 0;
    for (i = 0; i < count; i++) {
        _snwprintf(key, 32, L"Path%d", i);
        GetPrivateProfileStringW(L"Bookmarks", key, L"",
            app->bookmarks[app->bookmark_count], DABOM_MAX_PATH, app->config_path);
        if (app->bookmarks[app->bookmark_count][0])
            app->bookmark_count++;
    }
}

void dabom_save_settings(AppState *app)
{
    const wchar_t *sort;
    int i;
    wchar_t key[32];
    wchar_t count_buf[16];
    wchar_t page_buf[8];

    if (!app->config_path[0])
        return;
    WritePrivateProfileStringW(
        L"DaBom", L"LastFolder", app->last_folder, app->config_path);

    switch (app->sort_mode) {
    case SORT_DATE: sort = L"date"; break;
    case SORT_SIZE: sort = L"size"; break;
    default:        sort = L"name"; break;
    }
    WritePrivateProfileStringW(L"DaBom", L"SortMode", sort, app->config_path);
    WritePrivateProfileStringW(L"DaBom", L"Smooth",
        app->smooth ? L"1" : L"0", app->config_path);
    WritePrivateProfileStringW(L"DaBom", L"Layout",
        layout_to_str(app->layout), app->config_path);
    _snwprintf(page_buf, 8, L"%d", (int)app->page_mode);
    WritePrivateProfileStringW(L"DaBom", L"PageMode", page_buf, app->config_path);

    _snwprintf(count_buf, 16, L"%d", app->bookmark_count);
    WritePrivateProfileStringW(L"Bookmarks", L"Count", count_buf, app->config_path);
    for (i = 0; i < app->bookmark_count; i++) {
        _snwprintf(key, 32, L"Path%d", i);
        WritePrivateProfileStringW(L"Bookmarks", key, app->bookmarks[i], app->config_path);
    }
}

int dabom_bookmark_find(AppState *app, const wchar_t *path)
{
    int i;
    if (!app || !path)
        return -1;
    for (i = 0; i < app->bookmark_count; i++) {
        if (_wcsicmp(app->bookmarks[i], path) == 0)
            return i;
    }
    return -1;
}

int dabom_bookmark_add(AppState *app, const wchar_t *path)
{
    if (!app || !path || !path[0])
        return 0;
    if (dabom_bookmark_find(app, path) >= 0)
        return 1;
    if (app->bookmark_count >= DABOM_MAX_BOOKMARKS)
        return 0;
    wcsncpy(app->bookmarks[app->bookmark_count], path, DABOM_MAX_PATH - 1);
    app->bookmarks[app->bookmark_count][DABOM_MAX_PATH - 1] = 0;
    app->bookmark_count++;
    dabom_save_settings(app);
    return 1;
}
