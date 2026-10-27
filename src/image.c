#include "dabom.h"

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
