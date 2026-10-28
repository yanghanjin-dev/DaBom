#include "dabom.h"

enum { CTX_SEP_ID = ID_CTX_SEP };

typedef struct {
    UINT id;
    const wchar_t *label;    /* NULL = separator */
    const wchar_t *shortcut;
    int checkable;
} CtxDef;

/* Merged context menu (ref1 + ref2 unique + layout submenu). Icon strip → items. */
static const CtxDef g_ctx_main[] = {
    { ID_MENU_OPEN_FOLDER, L"폴더 열기",           NULL, 0 },
    { ID_CTX_DELETE,       L"파일 삭제",           L"Del", 0 },
    { 0, NULL, NULL, 0 },
    { ID_CTX_FULLSCREEN,   L"전체화면(X)",         L"Alt+Enter", 1 },
    { ID_CTX_CONTINUOUS,   L"연속보기(S)",         NULL, 1 },
    { 0, NULL, NULL, 0 },
    { ID_CTX_SMOOTH,       L"이미지를 부드럽게(M)", NULL, 1 },
    /* 이미지 배치(D) — submenu inserted separately */
    { ID_CTX_LAYOUT_FIT,   L"이미지 배치(D)",      NULL, 0 }, /* placeholder replaced */
    { 0, NULL, NULL, 0 },
    { ID_CTX_COPY,         L"클립보드에 복사(C)",   L"Ctrl+C", 0 },
    { ID_CTX_PASTE,        L"클립보드에서 붙여넣기", L"Ctrl+V", 0 },
    { ID_CTX_SAVE_AS,      L"다른 이름으로 저장(U)", L"Ctrl+S", 0 },
    { 0, NULL, NULL, 0 },
    { ID_CTX_ROTATE,       L"회전하기(T)",         L"Ctrl+T", 0 },
    { ID_CTX_ROTATE_CCW,   L"이미지 돌려보기",     NULL, 0 },
    { ID_CTX_RESIZE,       L"크기변경(R)",         L"Ctrl+R", 0 },
    { ID_CTX_CONVERT,      L"포맷변환(E)",         L"Ctrl+E", 0 },
    /* 이미지(Alt+I) submenu placeholder */
    { ID_CTX_IMG_ROTATE,   L"이미지(I)",           L"Alt+I", 0 },
    { 0, NULL, NULL, 0 },
    { ID_CTX_SORT_NAME,    L"정렬 방식(S)",        L"Alt+S", 0 }, /* submenu */
    { ID_CTX_HDR,          L"HDR(H)",              L"Alt+H", 1 },
    { ID_CTX_BOOKMARK_ADD, L"북마크 추가(B)",      L"Alt+B", 0 },
    { ID_CTX_BOOKMARK_JUMP,L"북마크(B)",           NULL, 0 }, /* submenu */
    { 0, NULL, NULL, 0 },
    { ID_CTX_PRINT,        L"인쇄하기(P)",         L"Ctrl+P", 0 },
    { ID_CTX_PIXEL,        L"픽셀정보(Q)",         L"Alt+P", 1 },
    { ID_CTX_CAMERA,       L"카메라 정보(I)",      L"F12", 0 },
    { ID_CTX_PROPS,        L"파일 속성(Z)",        NULL, 0 },
    { 0, NULL, NULL, 0 },
    { ID_CTX_EXPLORER,     L"탐색기 열기",         L"Ctrl+Enter", 0 },
    { ID_CTX_EDIT_APP,     L"편집 프로그램 실행",  L"Ctrl+Shift+E", 0 },
    { ID_CTX_LIBRARY,      L"사진 보관함으로 복사", L"Ins", 0 },
    { ID_CTX_WALLPAPER,     L"바탕 화면 배경으로 지정", NULL, 0 },
};

static const CtxDef g_layout_items[] = {
    { ID_CTX_LAYOUT_FIT_LARGE,   L"큰 이미지만 화면에 맞춤(U)", NULL, 1 },
    { ID_CTX_LAYOUT_FIT,         L"화면에 맞춤(F)",             NULL, 1 },
    { ID_CTX_LAYOUT_WIDTH,       L"화면 너비에 맞춤(W)",         NULL, 1 },
    { ID_CTX_LAYOUT_HEIGHT,      L"화면 높이에 맞춤(H)",         NULL, 1 },
    { ID_CTX_LAYOUT_ACTUAL_KEEP, L"원본크기로 계속 보기(O)",     NULL, 1 },
    { ID_CTX_LAYOUT_ZOOM_KEEP,   L"현재 배율 유지(L)",           NULL, 1 },
    { ID_CTX_LAYOUT_STRETCH,     L"창에 늘리기",                 NULL, 1 },
    { ID_CTX_LAYOUT_ACTUAL,      L"원본크기",                   NULL, 1 },
    { 0, NULL, NULL, 0 },
    { ID_CTX_PAGE_ONE,           L"한장보기(X)",  L"Ctrl+Alt+1", 1 },
    { ID_CTX_PAGE_TWO,           L"두장보기(Y)",  L"Ctrl+Alt+2", 1 },
    { ID_CTX_PAGE_FOUR,          L"네장보기(Z)",  L"Ctrl+Alt+4", 1 },
};

static const CtxDef g_sort_items[] = {
    { ID_CTX_SORT_NAME, L"파일명", NULL, 1 },
    { ID_CTX_SORT_DATE, L"날짜",   NULL, 1 },
    { ID_CTX_SORT_SIZE, L"크기",   NULL, 1 },
};

static const CtxDef g_img_items[] = {
    { ID_CTX_IMG_ROTATE,  L"회전하기",   L"Ctrl+T", 0 },
    { ID_CTX_IMG_RESIZE,  L"크기변경",   L"Ctrl+R", 0 },
    { ID_CTX_IMG_CONVERT, L"포맷변환",   L"Ctrl+E", 0 },
};

static MenuOdItem g_od_pool[128];
static int g_od_n;

static MenuOdItem *od_alloc(const wchar_t *label, const wchar_t *shortcut, int submenu)
{
    MenuOdItem *it;
    if (g_od_n >= 128)
        return NULL;
    it = &g_od_pool[g_od_n++];
    it->label = label;
    it->shortcut = shortcut;
    it->submenu = submenu ? 1 : 0;
    return it;
}

static void insert_od_item(HMENU menu, UINT pos, UINT id, const wchar_t *label,
    const wchar_t *shortcut, UINT state)
{
    MENUITEMINFOW mii;
    MenuOdItem *od = od_alloc(label, shortcut, 0);
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE;
    mii.fType = MFT_OWNERDRAW;
    mii.fState = state;
    mii.wID = id ? id : CTX_SEP_ID;
    mii.dwItemData = (ULONG_PTR)od;
    if (!label)
        mii.fState = MFS_DISABLED | MFS_GRAYED;
    InsertMenuItemW(menu, pos, TRUE, &mii);
}

static HMENU build_od_popup(AppState *app, const CtxDef *defs, int count)
{
    HMENU menu;
    MENUINFO mi;
    COLORREF bg;
    int i;

    menu = CreatePopupMenu();
    if (!menu) return NULL;

    bg = app->palette.dark ? RGB(0x25, 0x25, 0x26) : RGB(0xFF, 0xFF, 0xFF);
    if (!app->br_menu)
        app->br_menu = CreateSolidBrush(bg);
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_STYLE;
    mi.dwStyle = MNS_CHECKORBMP;
    mi.hbrBack = app->br_menu;
    SetMenuInfo(menu, &mi);

    for (i = 0; i < count; i++) {
        if (!defs[i].label) {
            insert_od_item(menu, (UINT)i, CTX_SEP_ID, NULL, NULL, 0);
        } else {
            insert_od_item(menu, (UINT)i, defs[i].id, defs[i].label,
                defs[i].shortcut, 0);
        }
    }
    return menu;
}

static void rebuild_bookmarks_submenu(AppState *app)
{
    int i;
    if (app->menu_bookmarks)
        DestroyMenu(app->menu_bookmarks);
    app->menu_bookmarks = CreatePopupMenu();
    if (!app->menu_bookmarks) return;

    {
        MENUINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_STYLE;
        mi.dwStyle = MNS_CHECKORBMP;
        mi.hbrBack = app->br_menu;
        SetMenuInfo(app->menu_bookmarks, &mi);
    }

    if (app->bookmark_count == 0) {
        insert_od_item(app->menu_bookmarks, 0, CTX_SEP_ID, L"(북마크 없음)", NULL,
            MFS_DISABLED | MFS_GRAYED);
        return;
    }
    for (i = 0; i < app->bookmark_count; i++) {
        const wchar_t *name = wcsrchr(app->bookmarks[i], L'\\');
        name = name ? name + 1 : app->bookmarks[i];
        insert_od_item(app->menu_bookmarks, (UINT)i,
            (UINT)(ID_CTX_BOOKMARK_BASE + i), name, NULL, 0);
    }
}

void dabom_ctxmenu_create(AppState *app)
{
    HMENU menu;
    MENUINFO mi;
    COLORREF bg;
    int i, pos;
    MENUITEMINFOW mii;

    dabom_ctxmenu_destroy(app);
    g_od_n = 0;

    bg = app->palette.dark ? RGB(0x25, 0x25, 0x26) : RGB(0xFF, 0xFF, 0xFF);
    if (app->br_menu) {
        DeleteObject(app->br_menu);
        app->br_menu = NULL;
    }
    app->br_menu = CreateSolidBrush(bg);

    menu = CreatePopupMenu();
    if (!menu) return;

    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_STYLE;
    mi.dwStyle = MNS_CHECKORBMP;
    mi.hbrBack = app->br_menu;
    SetMenuInfo(menu, &mi);

    app->menu_layout = build_od_popup(app, g_layout_items,
        (int)(sizeof(g_layout_items) / sizeof(g_layout_items[0])));
    app->menu_sort = build_od_popup(app, g_sort_items,
        (int)(sizeof(g_sort_items) / sizeof(g_sort_items[0])));
    app->menu_img = build_od_popup(app, g_img_items,
        (int)(sizeof(g_img_items) / sizeof(g_img_items[0])));
    rebuild_bookmarks_submenu(app);

    pos = 0;
    for (i = 0; i < (int)(sizeof(g_ctx_main) / sizeof(g_ctx_main[0])); i++) {
        if (!g_ctx_main[i].label) {
            insert_od_item(menu, (UINT)pos++, CTX_SEP_ID, NULL, NULL, 0);
            continue;
        }
        /* Replace placeholders with submenus. */
        if (g_ctx_main[i].id == ID_CTX_LAYOUT_FIT &&
            wcscmp(g_ctx_main[i].label, L"이미지 배치(D)") == 0) {
            ZeroMemory(&mii, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE | MIIM_SUBMENU;
            mii.fType = MFT_OWNERDRAW;
            mii.wID = ID_CTX_LAYOUT_FIT;
            mii.hSubMenu = app->menu_layout;
            mii.dwItemData = (ULONG_PTR)od_alloc(L"이미지 배치(D)", NULL, 1);
            InsertMenuItemW(menu, (UINT)pos++, TRUE, &mii);
            continue;
        }
        if (g_ctx_main[i].id == ID_CTX_IMG_ROTATE &&
            wcscmp(g_ctx_main[i].label, L"이미지(I)") == 0) {
            ZeroMemory(&mii, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE | MIIM_SUBMENU;
            mii.fType = MFT_OWNERDRAW;
            mii.wID = ID_CTX_IMG_ROTATE;
            mii.hSubMenu = app->menu_img;
            /* Submenu chevron wins over Alt+I shortcut column. */
            mii.dwItemData = (ULONG_PTR)od_alloc(L"이미지(I)", NULL, 1);
            InsertMenuItemW(menu, (UINT)pos++, TRUE, &mii);
            continue;
        }
        if (g_ctx_main[i].id == ID_CTX_SORT_NAME &&
            wcscmp(g_ctx_main[i].label, L"정렬 방식(S)") == 0) {
            ZeroMemory(&mii, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE | MIIM_SUBMENU;
            mii.fType = MFT_OWNERDRAW;
            mii.wID = ID_CTX_SORT_NAME;
            mii.hSubMenu = app->menu_sort;
            mii.dwItemData = (ULONG_PTR)od_alloc(L"정렬 방식(S)", NULL, 1);
            InsertMenuItemW(menu, (UINT)pos++, TRUE, &mii);
            continue;
        }
        if (g_ctx_main[i].id == ID_CTX_BOOKMARK_JUMP) {
            ZeroMemory(&mii, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE | MIIM_SUBMENU;
            mii.fType = MFT_OWNERDRAW;
            mii.wID = ID_CTX_BOOKMARK_JUMP;
            mii.hSubMenu = app->menu_bookmarks;
            mii.dwItemData = (ULONG_PTR)od_alloc(L"북마크 이동", NULL, 1);
            InsertMenuItemW(menu, (UINT)pos++, TRUE, &mii);
            continue;
        }
        insert_od_item(menu, (UINT)pos++, g_ctx_main[i].id, g_ctx_main[i].label,
            g_ctx_main[i].shortcut, 0);
    }

    app->menu_ctx = menu;
    dabom_ctxmenu_update_checks(app);
}

void dabom_ctxmenu_destroy(AppState *app)
{
    /* Destroying menu_ctx destroys owned submenus if they were attached.
       Detach first to avoid double-free when we track pointers. */
    if (app->menu_ctx) {
        DestroyMenu(app->menu_ctx);
        app->menu_ctx = NULL;
        app->menu_layout = NULL;
        app->menu_sort = NULL;
        app->menu_img = NULL;
        app->menu_bookmarks = NULL;
    }
}

void dabom_ctxmenu_theme(AppState *app)
{
    COLORREF bg;
    MENUINFO mi;
    if (!app->menu_ctx) return;
    bg = app->palette.dark ? RGB(0x25, 0x25, 0x26) : RGB(0xFF, 0xFF, 0xFF);
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
    SetMenuInfo(app->menu_ctx, &mi);
    if (app->menu_layout) SetMenuInfo(app->menu_layout, &mi);
    if (app->menu_sort) SetMenuInfo(app->menu_sort, &mi);
    if (app->menu_img) SetMenuInfo(app->menu_img, &mi);
    if (app->menu_bookmarks) SetMenuInfo(app->menu_bookmarks, &mi);
}

static void check_cmd(HMENU menu, UINT id, int on)
{
    if (!menu) return;
    CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

void dabom_ctxmenu_update_checks(AppState *app)
{
    if (!app->menu_ctx) return;
    check_cmd(app->menu_ctx, ID_CTX_FULLSCREEN, app->fullscreen);
    check_cmd(app->menu_ctx, ID_CTX_CONTINUOUS, app->continuous);
    check_cmd(app->menu_ctx, ID_CTX_SMOOTH, app->smooth);
    check_cmd(app->menu_ctx, ID_CTX_HDR, app->hdr_preview);
    check_cmd(app->menu_ctx, ID_CTX_PIXEL, app->pixel_info);

    check_cmd(app->menu_layout, ID_CTX_LAYOUT_FIT_LARGE, app->layout == LAYOUT_FIT_LARGE);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_FIT, app->layout == LAYOUT_FIT);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_WIDTH, app->layout == LAYOUT_FIT_WIDTH);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_HEIGHT, app->layout == LAYOUT_FIT_HEIGHT);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_ACTUAL_KEEP, app->layout == LAYOUT_ACTUAL_KEEP);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_ZOOM_KEEP, app->layout == LAYOUT_ZOOM_KEEP);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_STRETCH, app->layout == LAYOUT_STRETCH);
    check_cmd(app->menu_layout, ID_CTX_LAYOUT_ACTUAL, app->layout == LAYOUT_ACTUAL);
    check_cmd(app->menu_layout, ID_CTX_PAGE_ONE, app->page_mode == PAGE_ONE);
    check_cmd(app->menu_layout, ID_CTX_PAGE_TWO, app->page_mode == PAGE_TWO);
    check_cmd(app->menu_layout, ID_CTX_PAGE_FOUR, app->page_mode == PAGE_FOUR);

    check_cmd(app->menu_sort, ID_CTX_SORT_NAME, app->sort_mode == SORT_NAME);
    check_cmd(app->menu_sort, ID_CTX_SORT_DATE, app->sort_mode == SORT_DATE);
    check_cmd(app->menu_sort, ID_CTX_SORT_SIZE, app->sort_mode == SORT_SIZE);
}

void dabom_ctxmenu_popup(AppState *app, int x, int y)
{
    UINT cmd;
    if (!app || !app->hwnd)
        return;
    if (app->file_count <= 0 && app->mode == VIEW_EMPTY)
        return; /* don't steal when no image — allow empty only for paste/folder? */
    /* Allow on empty for paste/open folder; require content area hit with files for most. */
    if (!app->menu_ctx)
        dabom_ctxmenu_create(app);
    rebuild_bookmarks_submenu(app);
    /* Re-attach bookmarks submenu */
    if (app->menu_ctx && app->menu_bookmarks) {
        MENUITEMINFOW mii;
        ZeroMemory(&mii, sizeof(mii));
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_SUBMENU;
        mii.hSubMenu = app->menu_bookmarks;
        SetMenuItemInfoW(app->menu_ctx, ID_CTX_BOOKMARK_JUMP, FALSE, &mii);
    }
    dabom_ctxmenu_update_checks(app);

    cmd = (UINT)dabom_track_popup_menu(app, app->menu_ctx,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        x, y);
    if (cmd)
        dabom_handle_ctx_command(app, cmd);
}
