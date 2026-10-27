#include "dabom.h"

int dabom_is_image_ext(const wchar_t *path)
{
    const wchar_t *dot = wcsrchr(path, L'.');
    if (!dot || !dot[1])
        return 0;
    wchar_t ext[16];
    size_t i;
    for (i = 0; i < 15 && dot[1 + i]; i++) {
        wchar_t c = dot[1 + i];
        if (c >= L'A' && c <= L'Z')
            c = (wchar_t)(c - L'A' + L'a');
        ext[i] = c;
    }
    ext[i] = 0;

    return wcscmp(ext, L"jpg") == 0
        || wcscmp(ext, L"jpeg") == 0
        || wcscmp(ext, L"png") == 0
        || wcscmp(ext, L"gif") == 0
        || wcscmp(ext, L"webp") == 0
        || wcscmp(ext, L"bmp") == 0;
}

void dabom_clear_files(AppState *app)
{
    int i;
    for (i = 0; i < app->file_count; i++)
        dabom_free_bitmap(&app->files[i].thumb);
    free(app->files);
    app->files = NULL;
    app->file_count = 0;
    app->file_cap = 0;
    app->current = 0;
    app->scroll_y = 0;
    dabom_free_bitmap(&app->full_bmp);
    app->full_w = app->full_h = 0;
}

int dabom_add_file(AppState *app, const wchar_t *path)
{
    int i;
    if (!dabom_is_image_ext(path))
        return 0;
    for (i = 0; i < app->file_count; i++) {
        if (_wcsicmp(app->files[i].path, path) == 0)
            return 0;
    }
    if (app->file_count >= DABOM_MAX_FILES)
        return 0;
    if (app->file_count >= app->file_cap) {
        int ncap = app->file_cap ? app->file_cap * 2 : 64;
        ImageEntry *nf = (ImageEntry *)realloc(app->files, (size_t)ncap * sizeof(ImageEntry));
        if (!nf)
            return 0;
        app->files = nf;
        app->file_cap = ncap;
    }
    ZeroMemory(&app->files[app->file_count], sizeof(ImageEntry));
    wcsncpy(app->files[app->file_count].path, path, DABOM_MAX_PATH - 1);
    app->files[app->file_count].path[DABOM_MAX_PATH - 1] = 0;
    app->file_count++;
    return 1;
}

static int cmp_path(const void *a, const void *b)
{
    const ImageEntry *ea = (const ImageEntry *)a;
    const ImageEntry *eb = (const ImageEntry *)b;
    return _wcsicmp(ea->path, eb->path);
}

int dabom_scan_folder(AppState *app, const wchar_t *folder)
{
    wchar_t pattern[DABOM_MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    dabom_clear_files(app);
    _snwprintf(pattern, DABOM_MAX_PATH, L"%s\\*", folder);
    pattern[DABOM_MAX_PATH - 1] = 0;

    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do {
        wchar_t full[DABOM_MAX_PATH];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        _snwprintf(full, DABOM_MAX_PATH, L"%s\\%s", folder, fd.cFileName);
        full[DABOM_MAX_PATH - 1] = 0;
        dabom_add_file(app, full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (app->file_count > 1)
        qsort(app->files, (size_t)app->file_count, sizeof(ImageEntry), cmp_path);

    wcsncpy(app->last_folder, folder, DABOM_MAX_PATH - 1);
    app->last_folder[DABOM_MAX_PATH - 1] = 0;
    dabom_save_settings(app);
    return app->file_count;
}

void dabom_ensure_thumb(AppState *app, int index)
{
    UINT w = 0, h = 0;
    if (index < 0 || index >= app->file_count)
        return;
    if (app->files[index].thumb)
        return;
    app->files[index].thumb = dabom_load_bitmap(
        app, app->files[index].path, &w, &h, DABOM_THUMB);
    app->files[index].thumb_w = (int)w;
    app->files[index].thumb_h = (int)h;
}

void dabom_load_current_full(AppState *app)
{
    UINT w = 0, h = 0;
    dabom_free_bitmap(&app->full_bmp);
    app->full_w = app->full_h = 0;
    if (app->current < 0 || app->current >= app->file_count)
        return;
    app->full_bmp = dabom_load_bitmap(
        app, app->files[app->current].path, &w, &h, 0);
    app->full_w = w;
    app->full_h = h;
    app->fit_mode = 1;
    app->zoom = 1.0;
    app->pan_x = app->pan_y = 0;
}
