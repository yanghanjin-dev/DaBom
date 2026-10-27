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
    if (!app->config_path[0])
        return;
    GetPrivateProfileStringW(
        L"DaBom", L"LastFolder", L"",
        app->last_folder, DABOM_MAX_PATH, app->config_path);
}

void dabom_save_settings(AppState *app)
{
    if (!app->config_path[0])
        return;
    WritePrivateProfileStringW(
        L"DaBom", L"LastFolder", app->last_folder, app->config_path);
}
