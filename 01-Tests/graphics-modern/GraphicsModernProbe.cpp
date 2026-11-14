// Bounded real hardware shader/draw/readback and HWND Present/Resize validation.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <d3d11.h>
#include <dxgi.h>
#include "VertexShader.h"
#include "PixelShader.h"

template<class T> static void Release(T *&Object)
{
    if(Object) {Object->Release();Object=NULL;}
}
static BOOL Hr(const char *Name,HRESULT Value)
{
    printf("%s|hresult=0x%08lX\n",Name,(ULONG)Value);
    return SUCCEEDED(Value);
}
static void Pump(void)
{
    MSG Message;
    while(PeekMessageW(&Message,NULL,0,0,PM_REMOVE)) {
        TranslateMessage(&Message);DispatchMessageW(&Message);
    }
}
static BOOL ResizeWindow(HWND Window,UINT Width,UINT Height)
{
    RECT Area={0,0,(LONG)Width,(LONG)Height};
    if(!AdjustWindowRect(&Area,WS_OVERLAPPEDWINDOW,FALSE))return FALSE;
    return SetWindowPos(Window,NULL,0,0,Area.right-Area.left,Area.bottom-Area.top,
        SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
}
static BOOL DrawReadback(ID3D11Device *Device,ID3D11DeviceContext *Context,
    ID3D11Texture2D *Target,ID3D11Texture2D *Staging,ID3D11RenderTargetView *View,
    ID3D11VertexShader *VS,ID3D11PixelShader *PS,ID3D11Buffer *ColorBuffer,
    ID3D11RasterizerState *Rasterizer,UINT Width,UINT Height,UINT Stage,UINT Pass)
{
    static const FLOAT Colors[2][4]={{1,0,0,1},{0,1,1,1}};
    static const BYTE Expected[2][4]={{255,0,0,255},{0,255,255,255}};
    const FLOAT Black[4]={0,0,0,0};
    D3D11_VIEWPORT Viewport={0,0,(FLOAT)Width,(FLOAT)Height,0,1};
    D3D11_MAPPED_SUBRESOURCE Mapped={0};
    HRESULT Result;
    DWORD Start;
    UINT X,Y,C,Bad=0;
    Context->ClearRenderTargetView(View,Black);
    Context->UpdateSubresource(ColorBuffer,0,NULL,Colors[Pass],0,0);
    Context->OMSetRenderTargets(1,&View,NULL);
    Context->RSSetViewports(1,&Viewport);
    Context->RSSetState(Rasterizer);
    Context->IASetInputLayout(NULL);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->VSSetShader(VS,NULL,0);
    Context->PSSetShader(PS,NULL,0);
    Context->PSSetConstantBuffers(0,1,&ColorBuffer);
    Context->Draw(3,0);
    Context->OMSetRenderTargets(0,NULL,NULL);
    Context->CopyResource(Staging,Target);
    Context->Flush();
    Start=GetTickCount();
    do {
        Result=Context->Map(Staging,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&Mapped);
        if(Result!=DXGI_ERROR_WAS_STILL_DRAWING)break;
        if(GetTickCount()-Start>=5000)break;
        Sleep(1);
    } while(Result==DXGI_ERROR_WAS_STILL_DRAWING);
    printf("READBACK|stage=%u|pass=%u|hresult=0x%08lX|pitch=%u\n",Stage,Pass,(ULONG)Result,Mapped.RowPitch);
    if(FAILED(Result)) {
        Hr("DEVICE_REMOVED_REASON",Device->GetDeviceRemovedReason());return FALSE;
    }
    if(!Mapped.pData || Mapped.RowPitch<Width*4) {
        Context->Unmap(Staging,0);return FALSE;
    }
    for(Y=0;Y<Height;++Y) {
        const BYTE *Row=(const BYTE*)Mapped.pData+(SIZE_T)Y*Mapped.RowPitch;
        for(X=0;X<Width;++X) {
            BOOL Match=TRUE;
            for(C=0;C<4;++C)if(Row[X*4+C]!=Expected[Pass][C])Match=FALSE;
            if(!Match)++Bad;
        }
    }
    Context->Unmap(Staging,0);
    printf("SHADER_PIXELS|stage=%u|pass=%u|checked=%u|mismatch=%u|expected_rgba=%u,%u,%u,%u\n",
        Stage,Pass,Width*Height,Bad,(UINT)Expected[Pass][0],(UINT)Expected[Pass][1],
        (UINT)Expected[Pass][2],(UINT)Expected[Pass][3]);
    return Bad==0;
}
static int Run(HWND Window)
{
    IDXGISwapChain *SwapChain=NULL;
    ID3D11Device *Device=NULL;
    ID3D11DeviceContext *Context=NULL;
    ID3D11VertexShader *VS=NULL;
    ID3D11PixelShader *PS=NULL;
    ID3D11Buffer *ColorBuffer=NULL;
    ID3D11RasterizerState *Rasterizer=NULL;
    ID3D11Texture2D *Target=NULL,*Staging=NULL;
    ID3D11RenderTargetView *View=NULL;
    IDXGIDevice *Dxgi=NULL;
    IDXGIAdapter *Adapter=NULL;
    DXGI_ADAPTER_DESC AdapterDesc={0};
    D3D_FEATURE_LEVEL Feature=(D3D_FEATURE_LEVEL)0;
    const D3D_FEATURE_LEVEL Levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
    DXGI_SWAP_CHAIN_DESC Desc={0};
    D3D11_BUFFER_DESC BufferDesc={0};
    D3D11_RASTERIZER_DESC RasterDesc;
    D3D11_TEXTURE2D_DESC TextureDesc={0};
    UINT Stage,Pass,Width=64,Height=64;
    HRESULT Result;
    int Exit=1;
    Desc.BufferDesc.Width=Width;Desc.BufferDesc.Height=Height;
    Desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count=1;Desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount=1;Desc.OutputWindow=Window;Desc.Windowed=TRUE;
    Desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    Result=D3D11CreateDeviceAndSwapChain(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,
        Levels,ARRAYSIZE(Levels),D3D11_SDK_VERSION,&Desc,&SwapChain,&Device,&Feature,&Context);
    printf("HARDWARE_DEVICE|hresult=0x%08lX|feature_level=0x%04X|no_software_fallback=1\n",(ULONG)Result,(UINT)Feature);
    if(FAILED(Result) || !SwapChain || !Device || !Context)goto Cleanup;
    if(!Hr("DXGI_DEVICE",Device->QueryInterface(__uuidof(IDXGIDevice),(void**)&Dxgi)) || !Dxgi)goto Cleanup;
    if(!Hr("DXGI_ADAPTER",Dxgi->GetAdapter(&Adapter)) || !Adapter)goto Cleanup;
    if(!Hr("ADAPTER_DESC",Adapter->GetDesc(&AdapterDesc)))goto Cleanup;
    printf("ADAPTER|vendor=0x%04X|device=0x%04X\n",AdapterDesc.VendorId,AdapterDesc.DeviceId);
    if(!Hr("VERTEX_SHADER",Device->CreateVertexShader(VertexShader,sizeof(VertexShader),NULL,&VS)) || !VS)goto Cleanup;
    if(!Hr("PIXEL_SHADER",Device->CreatePixelShader(PixelShader,sizeof(PixelShader),NULL,&PS)) || !PS)goto Cleanup;
    BufferDesc.ByteWidth=16;BufferDesc.Usage=D3D11_USAGE_DEFAULT;BufferDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(!Hr("COLOR_BUFFER",Device->CreateBuffer(&BufferDesc,NULL,&ColorBuffer)) || !ColorBuffer)goto Cleanup;
    ZeroMemory(&RasterDesc,sizeof(RasterDesc));
    RasterDesc.FillMode=D3D11_FILL_SOLID;RasterDesc.CullMode=D3D11_CULL_NONE;RasterDesc.DepthClipEnable=TRUE;
    if(!Hr("RASTERIZER",Device->CreateRasterizerState(&RasterDesc,&Rasterizer)) || !Rasterizer)goto Cleanup;
    ShowWindow(Window,SW_SHOWNOACTIVATE);
    for(Stage=0;Stage<2;++Stage) {
        if(Stage) {
            Context->ClearState();Context->Flush();
            Release(View);Release(Staging);Release(Target);
            Width=96;Height=80;
            if(!ResizeWindow(Window,Width,Height))goto Cleanup;
            Pump();
            if(!Hr("RESIZE_BUFFERS",SwapChain->ResizeBuffers(0,Width,Height,DXGI_FORMAT_UNKNOWN,0)))goto Cleanup;
        }
        if(!Hr("BACKBUFFER",SwapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&Target)) || !Target)goto Cleanup;
        Target->GetDesc(&TextureDesc);
        printf("BACKBUFFER_DESC|stage=%u|width=%u|height=%u|format=%u\n",Stage,TextureDesc.Width,TextureDesc.Height,(UINT)TextureDesc.Format);
        if(TextureDesc.Width!=Width || TextureDesc.Height!=Height || TextureDesc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)goto Cleanup;
        if(!Hr("RTV",Device->CreateRenderTargetView(Target,NULL,&View)) || !View)goto Cleanup;
        TextureDesc.Usage=D3D11_USAGE_STAGING;TextureDesc.BindFlags=0;
        TextureDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;TextureDesc.MiscFlags=0;
        if(!Hr("STAGING",Device->CreateTexture2D(&TextureDesc,NULL,&Staging)) || !Staging)goto Cleanup;
        for(Pass=0;Pass<2;++Pass) {
            Pump();if(!IsWindow(Window))goto Cleanup;
            if(!DrawReadback(Device,Context,Target,Staging,View,VS,PS,ColorBuffer,Rasterizer,Width,Height,Stage,Pass))goto Cleanup;
            Result=SwapChain->Present(0,0);
            printf("HWND_PRESENT|stage=%u|pass=%u|hresult=0x%08lX\n",Stage,Pass,(ULONG)Result);
            // DXGI_STATUS_OCCLUDED is not proof of presentation.
            if(Result!=S_OK)goto Cleanup;
        }
    }
    printf("PASS|hardware_shader_draw_readback_present_resize|draws=4|pixels_verified=23552|present_s_ok=4|resize_s_ok=1\n");
    printf("LIMIT|legacy HWND swapchain; no DComp, NT shared handles, D3D12, WDDM upgrade or screenshot verification\n");
    Exit=0;
Cleanup:
    if(Context) {Context->ClearState();Context->Flush();}
    Release(View);Release(Staging);Release(Target);Release(Rasterizer);
    Release(ColorBuffer);Release(PS);Release(VS);Release(Adapter);Release(Dxgi);
    Release(SwapChain);Release(Context);Release(Device);
    if(Exit)printf("FAIL|graphics pipeline incomplete\n");
    return Exit;
}
int main(int Count,char **Arguments)
{
    BOOL WantKex,HasKex=GetModuleHandleW(L"KexDll.dll")!=NULL;
    WNDCLASSW Class={0};
    HWND Window;
    int Result;
    if(Count!=2 || (strcmp(Arguments[1],"--native") && strcmp(Arguments[1],"--vxkex"))) {
        printf("Usage: GraphicsModernProbe.exe --native | --vxkex\n");return 2;
    }
    WantKex=strcmp(Arguments[1],"--vxkex")==0;
    printf("CONTEXT|mode=%s|bits=%u|kex_loaded=%u\n",WantKex?"vxkex":"native",(UINT)(sizeof(void*)*8),HasKex);
    if(HasKex!=WantKex) {printf("REFUSED|actual Kex activation differs from mode\n");return 2;}
    Class.lpfnWndProc=DefWindowProcW;Class.hInstance=GetModuleHandleW(NULL);Class.lpszClassName=L"KexGraphicsModernR1";
    if(!RegisterClassW(&Class)) {printf("REGISTER_WINDOW|win32=%lu\n",GetLastError());return 1;}
    Window=CreateWindowExW(WS_EX_NOACTIVATE,Class.lpszClassName,L"Kex graphics: 4 frames",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,128,128,NULL,NULL,Class.hInstance,NULL);
    if(!Window || !ResizeWindow(Window,64,64)) {
        printf("CREATE_WINDOW|win32=%lu\n",GetLastError());
        if(Window)DestroyWindow(Window);UnregisterClassW(Class.lpszClassName,Class.hInstance);return 1;
    }
    Result=Run(Window);
    if(IsWindow(Window))DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName,Class.hInstance);
    return Result;
}
