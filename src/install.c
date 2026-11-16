#include "dabom.h"

#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DaBom"

static void install_error(HWND owner, DWORD error)
{
    wchar_t detail[512] = L"", message[768];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, 0, detail, 512, NULL);
    swprintf(message, 768, L"설치/제거를 완료하지 못했습니다. (오류 %lu)\n%s\n실행 중인 DaBom을 닫고 다시 시도하세요.", error, detail);
    MessageBoxW(owner, message, L"DaBom", MB_OK | MB_ICONERROR);
}

static BOOL install_paths(wchar_t *dir, wchar_t *exe, wchar_t *link)
{
    wchar_t programs[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, dir)) ||
        FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, programs)))
        return FALSE;
    if (wcslen(dir) + 32 >= MAX_PATH || wcslen(programs) + 32 >= MAX_PATH)
        return FALSE;
    wcscat_s(dir, MAX_PATH, L"\\DaBom");
    swprintf(exe, MAX_PATH, L"%s\\dabom.exe", dir);
    swprintf(link, MAX_PATH, L"%s\\DaBom.lnk", programs);
    return TRUE;
}

static BOOL elevated(void)
{
    HANDLE token;
    TOKEN_ELEVATION value;
    DWORD size;
    BOOL result = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &value, sizeof(value), &size))
            result = value.TokenIsElevated;
        CloseHandle(token);
    }
    return result;
}

/* Returns only after UAC has accepted or cancelled the request. */
static BOOL launch_admin(HWND owner, const wchar_t *exe, const wchar_t *args)
{
    SHELLEXECUTEINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.hwnd = owner;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = args;
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return TRUE;
    if (GetLastError() != ERROR_CANCELLED) install_error(owner, GetLastError());
    return FALSE;
}

void dabom_install_request(HWND owner, BOOL remove)
{
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return;
    if (remove && MessageBoxW(owner,
        L"DaBom을 모든 사용자에게서 제거할까요?\n이 창은 닫힙니다. 다른 DaBom 창도 닫아 주세요.\n사진과 개인 설정은 보존됩니다.",
        L"DaBom 제거", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (launch_admin(owner, self, remove ? L"--uninstall-confirmed" : L"--install") && remove && owner)
        PostMessageW(owner, WM_CLOSE, 0, 0);
}

static LONG reg_string(HKEY key, const wchar_t *name, const wchar_t *value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
        (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static HRESULT make_shortcut(const wchar_t *exe, const wchar_t *dir, const wchar_t *link)
{
    IShellLinkW *shell = NULL;
    IPersistFile *file = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
        &IID_IShellLinkW, (void **)&shell);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetPath(shell, exe);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetWorkingDirectory(shell, dir);
    if (SUCCEEDED(hr)) hr = IShellLinkW_SetIconLocation(shell, exe, 0);
    if (SUCCEEDED(hr)) hr = IShellLinkW_QueryInterface(shell, &IID_IPersistFile, (void **)&file);
    if (SUCCEEDED(hr)) hr = IPersistFile_Save(file, link, TRUE);
    if (file) IPersistFile_Release(file);
    if (shell) IShellLinkW_Release(shell);
    return hr;
}

/* Only fixed DaBom-owned files are removed; never recurse into user data. */
static DWORD remove_installation(const wchar_t *dir, const wchar_t *exe, const wchar_t *link)
{
    int retry;
    LONG status;
    for (retry = 0; retry < 100; ++retry) {
        if (DeleteFileW(exe) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
        if (GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_ACCESS_DENIED)
            return GetLastError();
        Sleep(100);
    }
    if (retry == 100) return ERROR_SHARING_VIOLATION;
    if (!DeleteFileW(link) && GetLastError() != ERROR_FILE_NOT_FOUND) return GetLastError();
    status = RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) return status;
    /* Keep a directory containing any files not owned by this installer. */
    if (!RemoveDirectoryW(dir) && GetLastError() != ERROR_DIR_NOT_EMPTY &&
        GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        return GetLastError();
    return ERROR_SUCCESS;
}

/* -1 means normal viewer startup. No shell or user-supplied deletion paths. */
int dabom_install_command(void)
{
    int argc, result = 1;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t self[MAX_PATH], dir[MAX_PATH], exe[MAX_PATH], link[MAX_PATH], command[MAX_PATH + 32];
    BOOL remove, worker, confirmed;
    DWORD error = ERROR_SUCCESS;
    HKEY key;
    LONG status;
    if (!argv) return -1;
    if (argc != 2 || (wcscmp(argv[1], L"--install") &&
        wcscmp(argv[1], L"--uninstall") && wcscmp(argv[1], L"--uninstall-confirmed") &&
        wcscmp(argv[1], L"--uninstall-worker"))) {
        LocalFree(argv);
        return -1;
    }
    remove = wcscmp(argv[1], L"--install") != 0;
    worker = wcscmp(argv[1], L"--uninstall-worker") == 0;
    confirmed = wcscmp(argv[1], L"--uninstall-confirmed") == 0;
    LocalFree(argv);
    if (!GetModuleFileNameW(NULL, self, MAX_PATH) || !install_paths(dir, exe, link)) {
        install_error(NULL, ERROR_PATH_NOT_FOUND);
        return 1;
    }
    if (!elevated()) {
        return launch_admin(NULL, self, remove ? L"--uninstall" : L"--install") ? 0 : 1;
    }
    if (!worker && !confirmed && MessageBoxW(NULL, remove ?
        L"DaBom을 모든 사용자에게서 제거할까요?\n실행 중인 DaBom을 닫아 주세요. 사진과 개인 설정은 보존됩니다." :
        L"DaBom을 모든 사용자용으로 설치할까요?\nProgram Files에 복사하고 시작 메뉴와 설치된 앱에 등록합니다.",
        L"DaBom", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;

    if (remove && !worker) {
        wchar_t temp[MAX_PATH], placeholder[MAX_PATH], copy[MAX_PATH];
        /* Run a temporary copy so the installed executable can be deleted. */
        DWORD length = GetTempPathW(MAX_PATH, temp);
        if (!length || length + 32 >= MAX_PATH) {
            install_error(NULL, ERROR_BAD_PATHNAME); return 1;
        }
        if (!GetTempFileNameW(temp, L"dbm", 0, placeholder)) {
            install_error(NULL, GetLastError()); return 1;
        }
        swprintf(copy, MAX_PATH, L"%s.exe", placeholder);
        DeleteFileW(placeholder);
        if (!CopyFileW(self, copy, TRUE)) {
            error = GetLastError(); DeleteFileW(copy); install_error(NULL, error); return 1;
        }
        if (!launch_admin(NULL, copy, L"--uninstall-worker")) { DeleteFileW(copy); return 1; }
        /* Temporary executable is reclaimed at the next restart. */
        MoveFileExW(copy, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        return 0;
    }
    if (remove) {
        error = remove_installation(dir, exe, link);
    } else {
        HRESULT hr;
        if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            error = GetLastError();
        if (!error && _wcsicmp(self, exe) && !CopyFileW(self, exe, FALSE)) error = GetLastError();
        if (!error) {
            hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            if (SUCCEEDED(hr)) {
                HRESULT shortcut = make_shortcut(exe, dir, link);
                CoUninitialize();
                if (FAILED(shortcut)) error = HRESULT_CODE(shortcut);
            } else error = HRESULT_CODE(hr);
        }
        if (!error) {
            status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, NULL, 0,
                KEY_WRITE, NULL, &key, NULL);
            if (status == ERROR_SUCCESS) {
                swprintf(command, MAX_PATH + 32, L"\"%s\" --uninstall", exe);
                status = reg_string(key, L"DisplayName", L"DaBom");
                if (!status) status = reg_string(key, L"DisplayVersion", DABOM_VERSION_W);
                if (!status) status = reg_string(key, L"InstallLocation", dir);
                if (!status) status = reg_string(key, L"DisplayIcon", exe);
                if (!status) status = reg_string(key, L"UninstallString", command);
                RegCloseKey(key);
            }
            error = status;
        }
    }
    if (error) install_error(NULL, error);
    else {
        MessageBoxW(NULL, remove ? L"DaBom 제거를 완료했습니다." :
            L"DaBom 설치를 완료했습니다. 시작 메뉴에서 실행할 수 있습니다.",
            L"DaBom", MB_OK | MB_ICONINFORMATION);
        result = 0;
    }
    return result;
}
