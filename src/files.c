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

static void fill_file_meta(ImageEntry *e)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    e->file_size = 0;
    ZeroMemory(&e->mtime, sizeof(e->mtime));
    if (GetFileAttributesExW(e->path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER ul;
        ul.LowPart = fad.nFileSizeLow;
        ul.HighPart = fad.nFileSizeHigh;
        e->file_size = ul.QuadPart;
        e->mtime = fad.ftLastWriteTime;
    }
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
    dabom_free_page_bitmaps(app);
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
    fill_file_meta(&app->files[app->file_count]);
    app->file_count++;
    return 1;
}

static int cmp_name(const void *a, const void *b)
{
    const ImageEntry *ea = (const ImageEntry *)a;
    const ImageEntry *eb = (const ImageEntry *)b;
    return _wcsicmp(ea->path, eb->path);
}

static int cmp_date(const void *a, const void *b)
{
    const ImageEntry *ea = (const ImageEntry *)a;
    const ImageEntry *eb = (const ImageEntry *)b;
    LONG r = CompareFileTime(&eb->mtime, &ea->mtime); /* newest first */
    if (r != 0) return (int)r;
    return _wcsicmp(ea->path, eb->path);
}

static int cmp_size(const void *a, const void *b)
{
    const ImageEntry *ea = (const ImageEntry *)a;
    const ImageEntry *eb = (const ImageEntry *)b;
    if (ea->file_size < eb->file_size) return 1;  /* larger first */
    if (ea->file_size > eb->file_size) return -1;
    return _wcsicmp(ea->path, eb->path);
}

void dabom_sort_files(AppState *app)
{
    wchar_t cur[DABOM_MAX_PATH];
    int i;

    if (!app || app->file_count <= 1)
        return;

    cur[0] = 0;
    if (app->current >= 0 && app->current < app->file_count)
        wcsncpy(cur, app->files[app->current].path, DABOM_MAX_PATH - 1);

    switch (app->sort_mode) {
    case SORT_DATE: qsort(app->files, (size_t)app->file_count, sizeof(ImageEntry), cmp_date); break;
    case SORT_SIZE: qsort(app->files, (size_t)app->file_count, sizeof(ImageEntry), cmp_size); break;
    case SORT_NAME:
    default:        qsort(app->files, (size_t)app->file_count, sizeof(ImageEntry), cmp_name); break;
    }

    if (cur[0]) {
        for (i = 0; i < app->file_count; i++) {
            if (_wcsicmp(app->files[i].path, cur) == 0) {
                app->current = i;
                break;
            }
        }
    }
}

void dabom_set_sort_mode(AppState *app, SortMode mode)
{
    if (!app)
        return;
    app->sort_mode = mode;
    dabom_sort_files(app);
    dabom_save_settings(app);
    dabom_update_chrome(app);
    InvalidateRect(app->hwnd, NULL, TRUE);
}

int dabom_remove_file_at(AppState *app, int index)
{
    int i;
    if (!app || index < 0 || index >= app->file_count)
        return 0;
    dabom_free_bitmap(&app->files[index].thumb);
    for (i = index; i < app->file_count - 1; i++)
        app->files[i] = app->files[i + 1];
    app->file_count--;
    if (app->file_count == 0) {
        app->current = 0;
        dabom_free_bitmap(&app->full_bmp);
        app->full_w = app->full_h = 0;
        dabom_set_mode(app, VIEW_EMPTY);
        return 1;
    }
    if (app->current >= app->file_count)
        app->current = app->file_count - 1;
    else if (app->current > index)
        app->current--;
    dabom_load_current_full(app);
    return 1;
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

    dabom_sort_files(app);

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

void dabom_free_page_bitmaps(AppState *app)
{
    int i;
    for (i = 0; i < 4; i++) {
        dabom_free_bitmap(&app->page_bmp[i]);
        app->page_w[i] = app->page_h[i] = 0;
        app->page_index[i] = -1;
    }
}

void dabom_load_current_full(AppState *app)
{
    UINT w = 0, h = 0;
    LayoutMode keep = app->layout;
    double keep_zoom = app->zoom;
    int keep_nav = (keep == LAYOUT_ACTUAL_KEEP || keep == LAYOUT_ZOOM_KEEP);

    dabom_free_bitmap(&app->full_bmp);
    app->full_w = app->full_h = 0;
    dabom_free_page_bitmaps(app);
    if (app->current < 0 || app->current >= app->file_count)
        return;
    app->full_bmp = dabom_load_bitmap(
        app, app->files[app->current].path, &w, &h, 0);
    app->full_w = w;
    app->full_h = h;

    if (keep_nav) {
        app->layout = keep;
        if (keep == LAYOUT_ACTUAL_KEEP) {
            app->zoom = 1.0;
            app->fit_mode = 0;
        } else {
            app->zoom = keep_zoom;
            app->fit_mode = 0;
        }
        app->pan_x = app->pan_y = 0;
    } else if (keep == LAYOUT_STRETCH || keep == LAYOUT_FIT_WIDTH ||
               keep == LAYOUT_FIT_HEIGHT || keep == LAYOUT_FIT_LARGE ||
               keep == LAYOUT_FIT) {
        app->layout = keep;
        app->fit_mode = 1;
        app->zoom = 1.0;
        app->pan_x = app->pan_y = 0;
    } else {
        app->layout = LAYOUT_FIT;
        app->fit_mode = 1;
        app->zoom = 1.0;
        app->pan_x = app->pan_y = 0;
    }

    if (app->page_mode != PAGE_ONE || app->continuous)
        dabom_load_page_bitmaps(app);
}

void dabom_load_page_bitmaps(AppState *app)
{
    int slots = (int)app->page_mode;
    int i;

    if (app->continuous)
        slots = 4; /* preload a few for continuous strip */
    if (slots < 1) slots = 1;
    if (slots > 4) slots = 4;

    dabom_free_page_bitmaps(app);
    for (i = 0; i < slots; i++) {
        int idx = app->current + i;
        UINT w = 0, h = 0;
        if (idx < 0 || idx >= app->file_count) {
            app->page_index[i] = -1;
            continue;
        }
        if (i == 0 && app->full_bmp) {
            /* Share current full bitmap into slot 0 (do not free with pages). */
            app->page_bmp[i] = NULL; /* paint uses full_bmp for slot 0 */
            app->page_w[i] = app->full_w;
            app->page_h[i] = app->full_h;
            app->page_index[i] = idx;
            continue;
        }
        app->page_bmp[i] = dabom_load_bitmap(app, app->files[idx].path, &w, &h, 0);
        app->page_w[i] = w;
        app->page_h[i] = h;
        app->page_index[i] = idx;
    }
}
