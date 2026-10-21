// hdr_capture.cpp: a full-resolution PNG of the main display as it is shown, for checking the overlay's look next to
// other mods (Spotter draws in its own Present hook, after the overlay, so only the finished picture has both). On an
// HDR desktop the desktop is read in linear light (DXGI desktop duplication, 16-bit float, scRGB) and brought back to
// SDR at Windows' SDR white level; what lies above it (Windows Auto HDR brightens an SDR game's picture) is scaled down
// by its brightest channel rather than clipped per channel, so a bright orange stays orange instead of turning yellow,
// as it does in captures that clip. On an SDR desktop the picture is taken as it is.
//   hdr_capture.exe <out.png>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

// Windows' SDR white level for the display of this output, in nits; 80 when it cannot be read.
static float SdrWhiteNits(const wchar_t *device)
{
    UINT32 paths = 0, modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS) return 80.0f;
    std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, p.data(), &modes, m.data(), nullptr) != ERROR_SUCCESS) return 80.0f;
    for (UINT32 i = 0; i < paths; i++)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = p[i].sourceInfo.adapterId;
        src.header.id = p[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName, device) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof(white);
        white.header.adapterId = p[i].targetInfo.adapterId;
        white.header.id = p[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS) return white.SDRWhiteLevel / 1000.0f * 80.0f;
    }
    return 80.0f;
}

static float Half(uint16_t h)
{
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, f = h & 1023;
    float v = e == 0 ? f / 1024.0f * powf(2.0f, -14.0f) : e == 31 ? 65504.0f : (1.0f + f / 1024.0f) * powf(2.0f, (float)e - 15.0f);
    return s ? -v : v;
}

static uint8_t Srgb(float v)
{
    v = v <= 0.0f ? 0.0f : v >= 1.0f ? 1.0f : v;
    const float e = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)(e * 255.0f + 0.5f);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2)
    {
        printf("hdr_capture.exe <out.png>\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
    {
        printf("FAIL no device\n");
        return 1;
    }
    IDXGIDevice *xdev = nullptr;
    IDXGIAdapter *adapter = nullptr;
    IDXGIOutput *out = nullptr;
    IDXGIOutput6 *out6 = nullptr;
    dev->QueryInterface(IID_PPV_ARGS(&xdev));
    xdev->GetAdapter(&adapter);
    if (FAILED(adapter->EnumOutputs(0, &out)) || FAILED(out->QueryInterface(IID_PPV_ARGS(&out6))))
    {
        printf("FAIL no output\n");
        return 1;
    }
    DXGI_OUTPUT_DESC1 od = {};
    out6->GetDesc1(&od);
    const bool hdr = od.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    const DXGI_FORMAT formats[2] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM };
    IDXGIOutputDuplication *dup = nullptr;
    HRESULT hr = out6->DuplicateOutput1(dev, 0, 1, hdr ? &formats[0] : &formats[1], &dup);
    if (FAILED(hr))
    {
        printf("FAIL duplication %08lx\n", (unsigned long)hr);
        return 1;
    }
    DXGI_OUTDUPL_DESC dd = {};
    dup->GetDesc(&dd);
    const float white = hdr ? SdrWhiteNits(od.DeviceName) : 80.0f;
    printf("display %ls: %ux%u, %s, SDR white %.0f nits\n", od.DeviceName, dd.ModeDesc.Width, dd.ModeDesc.Height, hdr ? "HDR" : "SDR", white);
    // a frame with the picture in it: the first one can be empty
    ID3D11Texture2D *frame = nullptr;
    for (int tries = 0; tries < 20 && !frame; tries++)
    {
        IDXGIResource *res = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info = {};
        if (FAILED(dup->AcquireNextFrame(500, &info, &res))) continue;
        if (info.LastPresentTime.QuadPart) res->QueryInterface(IID_PPV_ARGS(&frame));
        res->Release();
        if (!frame) dup->ReleaseFrame();
    }
    if (!frame)
    {
        printf("FAIL no frame\n");
        return 1;
    }
    D3D11_TEXTURE2D_DESC td = {};
    frame->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr;
    dev->CreateTexture2D(&td, nullptr, &staging);
    ctx->CopyResource(staging, frame);
    dup->ReleaseFrame();
    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        printf("FAIL map\n");
        return 1;
    }
    const UINT w = td.Width, h = td.Height;
    std::vector<uint8_t> bgra((size_t)w * h * 4);
    const float k = 80.0f / white;
    for (UINT y = 0; y < h; y++)
    {
        const uint8_t *row = (const uint8_t *)map.pData + (size_t)y * map.RowPitch;
        uint8_t *d = bgra.data() + (size_t)y * w * 4;
        for (UINT x = 0; x < w; x++, d += 4)
        {
            if (td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
                const uint16_t *s = (const uint16_t *)row + x * 4;
                float r = Half(s[0]) * k, g = Half(s[1]) * k, b = Half(s[2]) * k;
                const float most = fmaxf(r, fmaxf(g, b));
                if (most > 1.0f) { r /= most; g /= most; b /= most; }
                d[0] = Srgb(b);
                d[1] = Srgb(g);
                d[2] = Srgb(r);
            }
            else
            {
                const uint8_t *s = row + x * 4;
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
            }
            d[3] = 255;
        }
    }
    ctx->Unmap(staging, 0);
    IWICImagingFactory *wic = nullptr;
    IWICStream *st = nullptr;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *fr = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    const bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) && SUCCEEDED(wic->CreateStream(&st)) &&
                    SUCCEEDED(st->InitializeFromFilename(argv[1], GENERIC_WRITE)) && SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                    SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) &&
                    SUCCEEDED(fr->SetSize(w, h)) && SUCCEEDED(fr->SetPixelFormat(&fmt)) && SUCCEEDED(fr->WritePixels(h, w * 4, w * h * 4, bgra.data())) &&
                    SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    printf(ok ? "wrote %ls (%ux%u)\n" : "FAIL writing %ls\n", argv[1], w, h);
    return ok ? 0 : 1;
}
