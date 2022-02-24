#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <signal.h>
#include <glfw/glfw3.h>

#define COBJMACROS
    #pragma warning(push)
    #pragma warning(disable:4115) // named type definition in parentheses
    #include <d3d12.h>
    #include <d3dcompiler.h>
    #include <dxgi1_6.h>
    #include <d3d12sdklayers.h>
    #pragma warning(pop)
#undef COBJMACROS

#define HD_EXIT_FAILURE -1
#define HD_EXIT_SUCCESS 0

#define ExitOnFailure(expression) if (FAILED(expression)) raise(SIGINT);

IDXGIAdapter4* GetAdapter()
{
    IDXGIFactory4* dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ExitOnFailure(CreateDXGIFactory2(createFactoryFlags, &IID_IDXGIFactory4, &dxgiFactory));

    IDXGIAdapter1* dxgiAdapter1;
    IDXGIAdapter4* dxgiAdapter4;

    SIZE_T maxDedicatedVideoMemory = 0;
    for (UINT i = 0; IDXGIFactory1_EnumAdapters1(dxgiFactory, i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
        IDXGIAdapter1_GetDesc1(dxgiAdapter1, &dxgiAdapterDesc1);

        if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(D3D12CreateDevice(dxgiAdapter1,
                                        D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL)) &&
            dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory )
        {
            maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
            dxgiAdapter4 = (IDXGIAdapter4*)dxgiAdapter1;
        }
    }

    IDXGIFactory4_Release(dxgiFactory);

    return dxgiAdapter4;
}

ID3D12Device2* CreateDevice(IDXGIAdapter4* adapter)
{
    ID3D12Device2* d3d12Device2;
    ExitOnFailure(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, &d3d12Device2));

    // Enable debug messages in debug mode.
#if defined(_DEBUG)
    ID3D12InfoQueue* pInfoQueue;

    if (SUCCEEDED(ID3D12Device2_QueryInterface(d3d12Device2, &IID_ID3D12InfoQueue, &pInfoQueue)))
    {
        ID3D12InfoQueue_SetBreakOnSeverity(pInfoQueue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D12InfoQueue_SetBreakOnSeverity(pInfoQueue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D12InfoQueue_SetBreakOnSeverity(pInfoQueue, D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO
        };

        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {0};
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        ExitOnFailure(ID3D12InfoQueue_PushStorageFilter(pInfoQueue, &NewFilter));
    }
#endif

    return d3d12Device2;
}

ID3D12CommandQueue* CreateCommandQueue(ID3D12Device2* device, D3D12_COMMAND_LIST_TYPE type)
{
    ID3D12CommandQueue* d3d12CommandQueue;

    D3D12_COMMAND_QUEUE_DESC desc = {0};
    desc.Type =     type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags =    D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    ExitOnFailure(ID3D12Device2_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, &d3d12CommandQueue));

    return d3d12CommandQueue;
}

int main()
{
    GLFWwindow* window;
    if (!glfwInit())
        exit(HD_EXIT_FAILURE);

    const uint32_t width = 1280;
    const uint32_t height = 720;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(width, height, "Hello D3D12!", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(HD_EXIT_FAILURE);
    }
    glfwSwapInterval(1);
    HWND hWnd = glfwGetWin32Window(window);

    IDXGIAdapter4* dxgiAdapter4 = GetAdapter();
    ID3D12Device2* device = CreateDevice(dxgiAdapter4);
    ID3D12CommandQueue* g_CommandQueue = CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    IDXGIAdapter4_Release(dxgiAdapter4);
    ID3D12Device2_Release(device);
    ID3D12CommandQueue_Release(g_CommandQueue);

    return 0;
}