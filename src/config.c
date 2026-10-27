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

void dabom_load_settings(AppState *app)
{
    int pt;

    if (!app->config_path[0]) {
        app->ui_font_size_pt = DABOM_UI_FONT_PT_DEFAULT;
        return;
    }
    GetPrivateProfileStringW(
        L"DaBom", L"LastFolder", L"",
        app->last_folder, DABOM_MAX_PATH, app->config_path);

    /* Independent UI font size (points). Not related to Cursor Agents. */
    pt = (int)GetPrivateProfileIntW(
        L"DaBom", L"ui_font_size_pt", DABOM_UI_FONT_PT_DEFAULT, app->config_path);
    if (pt < DABOM_UI_FONT_PT_MIN)
        pt = DABOM_UI_FONT_PT_MIN;
    if (pt > DABOM_UI_FONT_PT_MAX)
        pt = DABOM_UI_FONT_PT_MAX;
    app->ui_font_size_pt = pt;
}

void dabom_save_settings(AppState *app)
{
    wchar_t buf[32];

    if (!app->config_path[0])
        return;
    WritePrivateProfileStringW(
        L"DaBom", L"LastFolder", app->last_folder, app->config_path);

    _snwprintf(buf, 32, L"%d", app->ui_font_size_pt);
    buf[31] = 0;
    WritePrivateProfileStringW(
        L"DaBom", L"ui_font_size_pt", buf, app->config_path);
}
