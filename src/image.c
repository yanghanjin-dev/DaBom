#include "dabom.h"
#include <propidl.h>

int dabom_wic_init(AppState *app)
{
    HRESULT hr = CoCreateInstance(
        &CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory,
        (void **)&app->wic);
    return SUCCEEDED(hr) && app->wic != NULL;
}

void dabom_wic_shutdown(AppState *app)
{
    if (app->wic) {
        IWICImagingFactory_Release(app->wic);
        app->wic = NULL;
    }
}

void dabom_free_bitmap(HBITMAP *pbmp)
{
    if (pbmp && *pbmp) {
        DeleteObject(*pbmp);
        *pbmp = NULL;
    }
}

static HBITMAP create_dib_from_source(IWICBitmapSource *source, UINT *out_w, UINT *out_h)
{
    UINT width = 0, height = 0;
    IWICBitmapSource_GetSize(source, &width, &height);
    if (width == 0 || height == 0)
        return NULL;

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = -(LONG)height; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HDC screen = GetDC(NULL);
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!bmp || !bits)
        return NULL;

    UINT stride = width * 4;
    UINT buf_size = stride * height;
    HRESULT hr = IWICBitmapSource_CopyPixels(source, NULL, stride, buf_size, (BYTE *)bits);
    if (FAILED(hr)) {
        DeleteObject(bmp);
        return NULL;
    }

    if (out_w) *out_w = width;
    if (out_h) *out_h = height;
    return bmp;
}

HBITMAP dabom_load_bitmap(AppState *app, const wchar_t *path, UINT *out_w, UINT *out_h, UINT max_side)
{
    if (!app->wic || !path || !path[0])
        return NULL;

    IWICBitmapDecoder *decoder = NULL;
    HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(
        app->wic, path, NULL, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder)
        return NULL;

    IWICBitmapFrameDecode *frame = NULL;
    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr) || !frame) {
        IWICBitmapDecoder_Release(decoder);
        return NULL;
    }

    IWICFormatConverter *converter = NULL;
    hr = IWICImagingFactory_CreateFormatConverter(app->wic, &converter);
    if (FAILED(hr) || !converter) {
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return NULL;
    }

    hr = IWICFormatConverter_Initialize(
        converter, (IWICBitmapSource *)frame,
        &GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, NULL, 0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return NULL;
    }

    IWICBitmapSource *src = (IWICBitmapSource *)converter;
    IWICBitmapScaler *scaler = NULL;
    UINT ow = 0, oh = 0;
    IWICBitmapSource_GetSize(src, &ow, &oh);

    if (max_side > 0 && (ow > max_side || oh > max_side)) {
        double scale = (double)max_side / (double)(ow > oh ? ow : oh);
        UINT nw = (UINT)(ow * scale);
        UINT nh = (UINT)(oh * scale);
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        hr = IWICImagingFactory_CreateBitmapScaler(app->wic, &scaler);
        if (SUCCEEDED(hr) && scaler) {
            hr = IWICBitmapScaler_Initialize(
                scaler, src, nw, nh, WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr))
                src = (IWICBitmapSource *)scaler;
        }
    }

    HBITMAP bmp = create_dib_from_source(src, out_w, out_h);

    if (scaler) IWICBitmapScaler_Release(scaler);
    IWICFormatConverter_Release(converter);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    return bmp;
}

/* Lock 32bpp top-down DIB bits (works for CreateDIBSection bitmaps). */
static int lock_dib32(HBITMAP bmp, BITMAP *bm, BYTE **bits, UINT *stride)
{
    if (!bmp || !bm || !bits)
        return 0;
    ZeroMemory(bm, sizeof(*bm));
    if (!GetObjectW(bmp, sizeof(*bm), bm) || !bm->bmBits || bm->bmBitsPixel != 32)
        return 0;
    *bits = (BYTE *)bm->bmBits;
    if (stride)
        *stride = (UINT)((bm->bmWidth * 4 + 3) & ~3);
    return 1;
}

HBITMAP dabom_rotate_bitmap_90cw(HBITMAP src, UINT *inout_w, UINT *inout_h)
{
    BITMAP sbm;
    BYTE *sbits = NULL;
    UINT sstride = 0;
    UINT sw, sh, dw, dh;
    BITMAPINFO bmi;
    void *dbits = NULL;
    HBITMAP dst;
    HDC screen;
    UINT x, y;

    if (!src || !inout_w || !inout_h)
        return NULL;
    sw = *inout_w;
    sh = *inout_h;
    if (sw == 0 || sh == 0)
        return NULL;
    if (!lock_dib32(src, &sbm, &sbits, &sstride))
        return NULL;

    dw = sh;
    dh = sw;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)dw;
    bmi.bmiHeader.biHeight = -(LONG)dh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    dst = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &dbits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!dst || !dbits)
        return NULL;

    for (y = 0; y < sh; y++) {
        for (x = 0; x < sw; x++) {
            BYTE *sp = sbits + y * sstride + x * 4;
            UINT dx = sh - 1 - y;
            UINT dy = x;
            BYTE *dp = (BYTE *)dbits + dy * (dw * 4) + dx * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }

    *inout_w = dw;
    *inout_h = dh;
    return dst;
}

HBITMAP dabom_rotate_bitmap_90ccw(HBITMAP src, UINT *inout_w, UINT *inout_h)
{
    /* Three CW rotations == one CCW; do direct CCW for speed. */
    BITMAP sbm;
    BYTE *sbits = NULL;
    UINT sstride = 0;
    UINT sw, sh, dw, dh;
    BITMAPINFO bmi;
    void *dbits = NULL;
    HBITMAP dst;
    HDC screen;
    UINT x, y;

    if (!src || !inout_w || !inout_h)
        return NULL;
    sw = *inout_w;
    sh = *inout_h;
    if (sw == 0 || sh == 0)
        return NULL;
    if (!lock_dib32(src, &sbm, &sbits, &sstride))
        return NULL;

    dw = sh;
    dh = sw;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)dw;
    bmi.bmiHeader.biHeight = -(LONG)dh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    dst = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &dbits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!dst || !dbits)
        return NULL;

    for (y = 0; y < sh; y++) {
        for (x = 0; x < sw; x++) {
            BYTE *sp = sbits + y * sstride + x * 4;
            UINT dx = y;
            UINT dy = sw - 1 - x;
            BYTE *dp = (BYTE *)dbits + dy * (dw * 4) + dx * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }

    *inout_w = dw;
    *inout_h = dh;
    return dst;
}

/* Simple HDR-ish preview: lift shadows + mild contrast (in-memory copy). */
HBITMAP dabom_hdr_preview_bitmap(HBITMAP src, UINT w, UINT h)
{
    BITMAP sbm;
    BYTE *sbits = NULL;
    UINT sstride = 0;
    BITMAPINFO bmi;
    void *dbits = NULL;
    HBITMAP dst;
    HDC screen;
    UINT x, y;

    if (!src || w == 0 || h == 0)
        return NULL;
    if (!lock_dib32(src, &sbm, &sbits, &sstride))
        return NULL;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    dst = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &dbits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!dst || !dbits)
        return NULL;

    for (y = 0; y < h; y++) {
        BYTE *srow = sbits + y * sstride;
        BYTE *drow = (BYTE *)dbits + y * (w * 4);
        for (x = 0; x < w; x++) {
            int b = srow[x * 4 + 0];
            int g = srow[x * 4 + 1];
            int r = srow[x * 4 + 2];
            /* Shadow lift + soft highlight compression. */
            r = r + (255 - r) / 5 + 12;
            g = g + (255 - g) / 5 + 12;
            b = b + (255 - b) / 5 + 12;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            drow[x * 4 + 0] = (BYTE)b;
            drow[x * 4 + 1] = (BYTE)g;
            drow[x * 4 + 2] = (BYTE)r;
            drow[x * 4 + 3] = srow[x * 4 + 3];
        }
    }
    return dst;
}

HBITMAP dabom_bitmap_from_clipboard(HWND hwnd, UINT *out_w, UINT *out_h)
{
    HBITMAP result = NULL;
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC screen, hdc_src, hdc_dst;
    HBITMAP src_bmp = NULL, old_s, old_d;
    BITMAP bm;
    UINT w, h;

    if (!OpenClipboard(hwnd))
        return NULL;

    /* Prefer CF_DIB. */
    {
        HANDLE hdib = GetClipboardData(CF_DIB);
        if (hdib) {
            BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)GlobalLock(hdib);
            if (bih) {
                BYTE *pixels;
                int topside;
                UINT stride;
                w = (UINT)bih->biWidth;
                h = (UINT)(bih->biHeight < 0 ? -bih->biHeight : bih->biHeight);
                topside = bih->biHeight < 0;
                stride = ((w * bih->biBitCount + 31) / 32) * 4;
                pixels = (BYTE *)bih + bih->biSize;
                if (bih->biBitCount == 32 && w > 0 && h > 0) {
                    ZeroMemory(&bmi, sizeof(bmi));
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = (LONG)w;
                    bmi.bmiHeader.biHeight = -(LONG)h;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    screen = GetDC(NULL);
                    result = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                    ReleaseDC(NULL, screen);
                    if (result && bits) {
                        UINT row;
                        for (row = 0; row < h; row++) {
                            BYTE *src = topside
                                ? pixels + row * stride
                                : pixels + (h - 1 - row) * stride;
                            memcpy((BYTE *)bits + row * (w * 4), src, w * 4);
                        }
                        if (out_w) *out_w = w;
                        if (out_h) *out_h = h;
                    }
                }
                GlobalUnlock(hdib);
            }
        }
    }

    if (!result) {
        src_bmp = (HBITMAP)GetClipboardData(CF_BITMAP);
        if (src_bmp) {
            ZeroMemory(&bm, sizeof(bm));
            GetObjectW(src_bmp, sizeof(bm), &bm);
            w = (UINT)bm.bmWidth;
            h = (UINT)bm.bmHeight;
            if (w > 0 && h > 0) {
                ZeroMemory(&bmi, sizeof(bmi));
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = (LONG)w;
                bmi.bmiHeader.biHeight = -(LONG)h;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                screen = GetDC(NULL);
                result = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                hdc_dst = CreateCompatibleDC(screen);
                hdc_src = CreateCompatibleDC(screen);
                ReleaseDC(NULL, screen);
                if (result) {
                    old_d = (HBITMAP)SelectObject(hdc_dst, result);
                    old_s = (HBITMAP)SelectObject(hdc_src, src_bmp);
                    BitBlt(hdc_dst, 0, 0, (int)w, (int)h, hdc_src, 0, 0, SRCCOPY);
                    SelectObject(hdc_dst, old_d);
                    SelectObject(hdc_src, old_s);
                    if (out_w) *out_w = w;
                    if (out_h) *out_h = h;
                }
                DeleteDC(hdc_dst);
                DeleteDC(hdc_src);
            }
        }
    }

    CloseClipboard();
    return result;
}

HBITMAP dabom_resize_bitmap(HBITMAP src, UINT src_w, UINT src_h, UINT dst_w, UINT dst_h, int smooth)
{
    BITMAPINFO bmi;
    void *bits = NULL;
    HBITMAP dst;
    HDC screen, hdc_dst, hdc_src;
    HBITMAP old_s, old_d;

    if (!src || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0)
        return NULL;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)dst_w;
    bmi.bmiHeader.biHeight = -(LONG)dst_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    dst = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!dst || !bits)
        return NULL;

    hdc_dst = CreateCompatibleDC(NULL);
    hdc_src = CreateCompatibleDC(NULL);
    old_d = (HBITMAP)SelectObject(hdc_dst, dst);
    old_s = (HBITMAP)SelectObject(hdc_src, src);
    SetStretchBltMode(hdc_dst, smooth ? HALFTONE : COLORONCOLOR);
    if (smooth)
        SetBrushOrgEx(hdc_dst, 0, 0, NULL);
    StretchBlt(hdc_dst, 0, 0, (int)dst_w, (int)dst_h,
        hdc_src, 0, 0, (int)src_w, (int)src_h, SRCCOPY);
    SelectObject(hdc_dst, old_d);
    SelectObject(hdc_src, old_s);
    DeleteDC(hdc_dst);
    DeleteDC(hdc_src);
    return dst;
}

static int container_from_path(const wchar_t *path, GUID *out)
{
    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext)
        return 0;
    if (_wcsicmp(ext, L".png") == 0) {
        *out = GUID_ContainerFormatPng;
        return 1;
    }
    if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) {
        *out = GUID_ContainerFormatJpeg;
        return 1;
    }
    if (_wcsicmp(ext, L".bmp") == 0) {
        *out = GUID_ContainerFormatBmp;
        return 1;
    }
    if (_wcsicmp(ext, L".webp") == 0) {
        *out = GUID_ContainerFormatWebp;
        return 1;
    }
    if (_wcsicmp(ext, L".gif") == 0) {
        *out = GUID_ContainerFormatGif;
        return 1;
    }
    if (_wcsicmp(ext, L".tif") == 0 || _wcsicmp(ext, L".tiff") == 0) {
        *out = GUID_ContainerFormatTiff;
        return 1;
    }
    return 0;
}

int dabom_save_bitmap(AppState *app, HBITMAP bmp, UINT w, UINT h,
    const wchar_t *path, const GUID *container_format)
{
    GUID container;
    BITMAP bm;
    BYTE *bits = NULL;
    UINT stride = 0;
    IWICBitmap *wic_bmp = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *props = NULL;
    HRESULT hr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;

    if (!app || !app->wic || !bmp || !path || !path[0] || w == 0 || h == 0)
        return 0;

    if (container_format)
        container = *container_format;
    else if (!container_from_path(path, &container))
        container = GUID_ContainerFormatPng;

    if (!lock_dib32(bmp, &bm, &bits, &stride))
        return 0;

    hr = IWICImagingFactory_CreateBitmapFromMemory(
        app->wic, w, h, &GUID_WICPixelFormat32bppBGRA,
        stride, stride * h, bits, &wic_bmp);
    if (FAILED(hr) || !wic_bmp)
        return 0;

    hr = IWICImagingFactory_CreateStream(app->wic, &stream);
    if (FAILED(hr) || !stream) {
        IWICBitmap_Release(wic_bmp);
        return 0;
    }
    hr = IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE);
    if (FAILED(hr)) {
        IWICStream_Release(stream);
        IWICBitmap_Release(wic_bmp);
        return 0;
    }

    hr = IWICImagingFactory_CreateEncoder(app->wic, &container, NULL, &encoder);
    if (FAILED(hr) || !encoder) {
        IWICStream_Release(stream);
        IWICBitmap_Release(wic_bmp);
        return 0;
    }
    hr = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        IWICBitmapEncoder_Release(encoder);
        IWICStream_Release(stream);
        IWICBitmap_Release(wic_bmp);
        return 0;
    }

    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &props);
    if (FAILED(hr) || !frame) {
        IWICBitmapEncoder_Release(encoder);
        IWICStream_Release(stream);
        IWICBitmap_Release(wic_bmp);
        return 0;
    }
    IWICBitmapFrameEncode_Initialize(frame, props);
    if (props)
        IPropertyBag2_Release(props);

    IWICBitmapFrameEncode_SetSize(frame, w, h);
    IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
    hr = IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource *)wic_bmp, NULL);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_Commit(encoder);

    IWICBitmapFrameEncode_Release(frame);
    IWICBitmapEncoder_Release(encoder);
    IWICStream_Release(stream);
    IWICBitmap_Release(wic_bmp);
    return SUCCEEDED(hr);
}

int dabom_get_pixel_rgb(HBITMAP bmp, UINT w, UINT h, int x, int y,
    int *r, int *g, int *b)
{
    BITMAP bm;
    BYTE *bits = NULL;
    UINT stride = 0;
    BYTE *p;

    if (!bmp || x < 0 || y < 0 || (UINT)x >= w || (UINT)y >= h)
        return 0;
    if (!lock_dib32(bmp, &bm, &bits, &stride))
        return 0;
    p = bits + (UINT)y * stride + (UINT)x * 4;
    if (b) *b = p[0];
    if (g) *g = p[1];
    if (r) *r = p[2];
    return 1;
}

int dabom_copy_bitmap_clipboard(HWND hwnd, HBITMAP bmp, UINT w, UINT h)
{
    BITMAPINFOHEADER bih;
    BITMAP bm;
    BYTE *bits = NULL;
    UINT stride = 0;
    HGLOBAL hmem;
    BYTE *dest;
    SIZE_T size;

    if (!bmp || w == 0 || h == 0)
        return 0;
    if (!lock_dib32(bmp, &bm, &bits, &stride))
        return 0;

    ZeroMemory(&bih, sizeof(bih));
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = (LONG)w;
    bih.biHeight = (LONG)h; /* bottom-up for CF_DIB */
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    size = sizeof(BITMAPINFOHEADER) + (SIZE_T)stride * h;
    hmem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hmem)
        return 0;
    dest = (BYTE *)GlobalLock(hmem);
    if (!dest) {
        GlobalFree(hmem);
        return 0;
    }
    memcpy(dest, &bih, sizeof(bih));
    /* Flip top-down → bottom-up rows for CF_DIB. */
    {
        UINT row;
        BYTE *out = dest + sizeof(bih);
        for (row = 0; row < h; row++) {
            memcpy(out + (SIZE_T)row * stride,
                bits + (SIZE_T)(h - 1 - row) * stride, stride);
        }
    }
    GlobalUnlock(hmem);

    if (!OpenClipboard(hwnd)) {
        GlobalFree(hmem);
        return 0;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIB, hmem)) {
        CloseClipboard();
        GlobalFree(hmem);
        return 0;
    }
    CloseClipboard();
    return 1;
}

/* Append one metadata string prop if present. */
static void append_meta_str(IWICMetadataQueryReader *reader,
    const wchar_t *path, const wchar_t *label, wchar_t *out, int cch)
{
    PROPVARIANT var;
    HRESULT hr;
    wchar_t line[256];
    size_t len, add;

    if (!reader || !out || cch <= 1)
        return;
    PropVariantInit(&var);
    hr = IWICMetadataQueryReader_GetMetadataByName(reader, path, &var);
    if (SUCCEEDED(hr)) {
        line[0] = 0;
        if (var.vt == VT_LPWSTR && var.pwszVal) {
            _snwprintf(line, 256, L"%s: %s\n", label, var.pwszVal);
        } else if (var.vt == (VT_VECTOR | VT_UI2) && var.caui.pElems && var.caui.cElems > 0) {
            /* Rational-ish vectors often used for exposure — show first. */
            _snwprintf(line, 256, L"%s: %u\n", label, (unsigned)var.caui.pElems[0]);
        } else if (var.vt == VT_UI2) {
            _snwprintf(line, 256, L"%s: %u\n", label, (unsigned)var.uiVal);
        } else if (var.vt == VT_UI4) {
            _snwprintf(line, 256, L"%s: %lu\n", label, (unsigned long)var.ulVal);
        } else if (var.vt == VT_R8) {
            _snwprintf(line, 256, L"%s: %.4f\n", label, var.dblVal);
        } else if (var.vt == VT_LPSTR && var.pszVal) {
            wchar_t tmp[128];
            MultiByteToWideChar(CP_ACP, 0, var.pszVal, -1, tmp, 128);
            _snwprintf(line, 256, L"%s: %s\n", label, tmp);
        }
        line[255] = 0;
        if (line[0]) {
            len = wcslen(out);
            add = wcslen(line);
            if ((int)(len + add + 1) < cch) {
                wcscat(out, line);
            }
        }
    }
    PropVariantClear(&var);
}

int dabom_read_camera_info(AppState *app, const wchar_t *path,
    wchar_t *out, int cch)
{
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICMetadataQueryReader *reader = NULL;
    HRESULT hr;

    if (!out || cch < 8)
        return 0;
    out[0] = 0;
    if (!app || !app->wic || !path || !path[0])
        return 0;

    hr = IWICImagingFactory_CreateDecoderFromFilename(
        app->wic, path, NULL, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder)
        return 0;

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr) || !frame) {
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    hr = IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &reader);
    if (FAILED(hr) || !reader) {
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    append_meta_str(reader, L"/app1/ifd/exif/{ushort=271}", L"제조사", out, cch);
    append_meta_str(reader, L"/app1/ifd/exif/{ushort=272}", L"모델", out, cch);
    append_meta_str(reader, L"/app1/ifd/{ushort=271}", L"제조사", out, cch);
    append_meta_str(reader, L"/app1/ifd/{ushort=272}", L"모델", out, cch);
    append_meta_str(reader, L"/app1/ifd/exif/{ushort=33434}", L"노출시간", out, cch);
    append_meta_str(reader, L"/app1/ifd/exif/{ushort=33437}", L"F값", out, cch);
    append_meta_str(reader, L"/app1/ifd/exif/{ushort=34855}", L"ISO", out, cch);
    append_meta_str(reader, L"/app1/ifd/exif/{ushort=37386}", L"초점거리", out, cch);
    append_meta_str(reader, L"/app1/ifd/exif/{ushort=36867}", L"촬영시각", out, cch);
    append_meta_str(reader, L"/ifd/exif/{ushort=271}", L"제조사", out, cch);
    append_meta_str(reader, L"/ifd/exif/{ushort=272}", L"모델", out, cch);
    append_meta_str(reader, L"/ifd/{ushort=271}", L"제조사", out, cch);
    append_meta_str(reader, L"/ifd/{ushort=272}", L"모델", out, cch);

    IWICMetadataQueryReader_Release(reader);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    return out[0] != 0;
}
