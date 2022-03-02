#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>
#include <cglm/cglm.h>

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

// Number of render targets
#define FRAMES_NUM 3

#define ExitOnFailure(expression) if (FAILED(expression)) raise(SIGINT);

typedef struct Vertex
{
    vec3 Position;
    vec3 Color;
} Vertex;

static Vertex g_Vertices[8] = {
    { {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 0.0f} }, // 0
    { {-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f} }, // 1
    { {1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f} }, // 2
    { {1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f} }, // 3
    { {-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f} }, // 4
    { {-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 1.0f} }, // 5
    { {1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 1.0f} }, // 6
    { {1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f} }  // 7
};

static WORD g_Indicies[36] =
{
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7
};

ID3D12Resource* g_BackBuffers[FRAMES_NUM];
ID3D12CommandAllocator* g_CommandAllocators[FRAMES_NUM];
uint64_t g_FrameFenceValues[FRAMES_NUM];
uint64_t g_FenceValue = 0;
UINT g_CurrentBackBufferIndex;
UINT g_RTVDescriptorSize;
UINT g_DSVDescriptorSize;
ID3D12DescriptorHeap* g_RTVDescriptorHeap;
ID3D12DescriptorHeap* g_DSVDescriptorHeap;
ID3D12Fence* g_Fence;
HANDLE g_FenceEvent;

void EnableDebuggingLayer()
{
    ID3D12Debug* debugInterface;
    ExitOnFailure(D3D12GetDebugInterface(&IID_ID3D12Debug, &debugInterface));
    ID3D12Debug_EnableDebugLayer(debugInterface);
}

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
            SUCCEEDED(D3D12CreateDevice((IUnknown*)dxgiAdapter1,
                                        D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL)) &&
            dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory )
        {
            maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;

            ExitOnFailure(IDXGIAdapter1_QueryInterface(dxgiAdapter1, &IID_IDXGIAdapter4, &dxgiAdapter4));
        }
    }

    IDXGIFactory4_Release(dxgiFactory);

    return dxgiAdapter4;
}

ID3D12Device2* CreateDevice(IDXGIAdapter4* adapter)
{
    ID3D12Device2* d3d12Device2;
    ExitOnFailure(D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device2, &d3d12Device2));

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

IDXGISwapChain4* CreateSwapChain(HWND hWnd,
                                 ID3D12CommandQueue* commandQueue,
                                 uint32_t width, uint32_t height, uint32_t bufferCount )
{
    IDXGISwapChain4* dxgiSwapChain4;
    IDXGIFactory4* dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ExitOnFailure(CreateDXGIFactory2(createFactoryFlags, &IID_IDXGIFactory4, &dxgiFactory4));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
            .Width = width,
            .Height = height,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .Stereo = FALSE,
            .SampleDesc = {
                .Count = 1,
                .Quality = 0,
            },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = bufferCount,
            .Scaling = DXGI_SCALING_STRETCH,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
            .Flags = 0
        };


    IDXGISwapChain1* swapChain1;
    ExitOnFailure(IDXGIFactory4_CreateSwapChainForHwnd(dxgiFactory4,
                                                       (IUnknown*)commandQueue,
                                                       hWnd,
                                                       &swapChainDesc,
                                                       NULL,
                                                       NULL,
                                                       &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    ExitOnFailure(IDXGIFactory4_MakeWindowAssociation(dxgiFactory4, hWnd, DXGI_MWA_NO_ALT_ENTER));

    ExitOnFailure(IDXGISwapChain1_QueryInterface(swapChain1, &IID_IDXGISwapChain1, &dxgiSwapChain4));

    return dxgiSwapChain4;
}

ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device2* device,
                                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                                           uint32_t numDescriptors)
{
    ID3D12DescriptorHeap* descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {0};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;

    ExitOnFailure(ID3D12Device2_CreateDescriptorHeap(device, &desc, &IID_ID3D12DescriptorHeap, &descriptorHeap));

    return descriptorHeap;
}

void UpdateRenderTargetViews(ID3D12Device2* device,
                             IDXGISwapChain4* swapChain, ID3D12DescriptorHeap* descriptorHeap)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptorHeap, &rtvHandle);

    for (int i = 0; i < FRAMES_NUM; ++i)
    {
        ID3D12Resource* backBuffer;
        ExitOnFailure(IDXGISwapChain4_GetBuffer(swapChain, i, &IID_ID3D12Resource, &backBuffer));

        ID3D12Device2_CreateRenderTargetView(device, backBuffer, NULL, rtvHandle);

        g_BackBuffers[i] = backBuffer;

        rtvHandle.ptr += g_RTVDescriptorSize;
    }
}

ID3D12CommandAllocator* CreateCommandAllocator(ID3D12Device2* device,
                                               D3D12_COMMAND_LIST_TYPE type)
{
    ID3D12CommandAllocator* commandAllocator;
    ExitOnFailure(ID3D12Device2_CreateCommandAllocator(device, type,
                                                       &IID_ID3D12CommandAllocator,
                                                       &commandAllocator));

    return commandAllocator;
}

ID3D12GraphicsCommandList* CreateCommandList(ID3D12Device2* device,
                                             ID3D12CommandAllocator* commandAllocator,
                                             D3D12_COMMAND_LIST_TYPE type)
{
    ID3D12GraphicsCommandList* commandList;
    ExitOnFailure(ID3D12Device2_CreateCommandList(device, 0, type, commandAllocator, NULL, &IID_ID3D12CommandList, &commandList));

    ExitOnFailure(ID3D12GraphicsCommandList_Close(commandList));

    return commandList;
}

ID3D12Fence* CreateFence(ID3D12Device2* device)
{
    // Reset fence values
    memset(g_FrameFenceValues, 0, FRAMES_NUM * sizeof(uint64_t));

    ID3D12Fence* fence;
    ExitOnFailure(ID3D12Device2_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &fence));

    return fence;
}

// Row-by-row memcpy
// Taken form d3dx12.h, rewritten for C
inline void MemcpySubresource(
    const D3D12_MEMCPY_DEST* pDest,
    const D3D12_SUBRESOURCE_DATA* pSrc,
    SIZE_T RowSizeInBytes,
    UINT NumRows,
    UINT NumSlices)
{
    for (UINT z = 0; z < NumSlices; ++z)
    {
        BYTE* pDestSlice = (BYTE*)(pDest->pData) + pDest->SlicePitch * z;
        const BYTE* pSrcSlice = (const BYTE*)(pSrc->pData) + pSrc->SlicePitch * z;
        for (UINT y = 0; y < NumRows; ++y)
        {
            memcpy(pDestSlice + pDest->RowPitch * y,
                   pSrcSlice + pSrc->RowPitch * y,
                   RowSizeInBytes);
        }
    }
}

// Taken form d3dx12.h, rewritten for C
inline UINT64 UpdateSubresourcesImpl(
    ID3D12GraphicsCommandList* pCmdList,
    ID3D12Resource* pDestinationResource,
    ID3D12Resource* pIntermediate,
    UINT FirstSubresource,
    UINT NumSubresources,
    UINT64 RequiredSize,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
    const UINT* pNumRows,
    const UINT64* pRowSizesInBytes,
    const D3D12_SUBRESOURCE_DATA* pSrcData)
{
    // Minor validation
    D3D12_RESOURCE_DESC IntermediateDesc;
    ID3D12Resource_GetDesc(pIntermediate, &IntermediateDesc);
    D3D12_RESOURCE_DESC DestinationDesc;
    ID3D12Resource_GetDesc(pDestinationResource, &DestinationDesc);
    if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset ||
        RequiredSize > (SIZE_T)-1 ||
        (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
            (FirstSubresource != 0 || NumSubresources != 1)))
    {
        return 0;
    }

    BYTE* pData;
    HRESULT hr = ID3D12Resource_Map(pIntermediate, 0, NULL, &pData);
    if (FAILED(hr))
    {
        return 0;
    }

    for (UINT i = 0; i < NumSubresources; ++i)
    {
        if (pRowSizesInBytes[i] > (SIZE_T)-1) return 0;
        D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, pLayouts[i].Footprint.RowPitch * pNumRows[i] };
        MemcpySubresource(&DestData, &pSrcData[i], (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);
    }
    ID3D12Resource_Unmap(pIntermediate, 0, NULL);

    if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        ID3D12GraphicsCommandList_CopyBufferRegion(pCmdList, pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
    }
    else
    {
        D3D12_TEXTURE_COPY_LOCATION Dst = {
            .pResource = pDestinationResource,
            .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        };

        D3D12_TEXTURE_COPY_LOCATION Src = {
            .pResource = pIntermediate,
            .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        };
        for (UINT i = 0; i < NumSubresources; ++i)
        {
            Dst.SubresourceIndex = FirstSubresource + i;
            Src.PlacedFootprint = pLayouts[i];

            ID3D12GraphicsCommandList_CopyTextureRegion(pCmdList, &Dst, 0, 0, 0, &Src, NULL);
        }
    }
    return RequiredSize;
}

// Heap-allocating UpdateSubresources implementation
// Taken form d3dx12.h, rewritten for C
inline UINT64 UpdateSubresources(
    ID3D12GraphicsCommandList* pCmdList,
    ID3D12Resource* pDestinationResource,
    ID3D12Resource* pIntermediate,
    UINT64 IntermediateOffset,
    UINT FirstSubresource,
    UINT NumSubresources,
    D3D12_SUBRESOURCE_DATA* pSrcData)
{
    UINT64 RequiredSize = 0;
    UINT64 MemToAlloc = (UINT64)(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
    if (MemToAlloc > SIZE_MAX)
    {
       return 0;
    }
    void* pMem = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)MemToAlloc);
    if (pMem == NULL)
    {
       return 0;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = (D3D12_PLACED_SUBRESOURCE_FOOTPRINT*)pMem;
    UINT64* pRowSizesInBytes = (UINT64*)(pLayouts + NumSubresources);
    UINT* pNumRows = (UINT*)(pRowSizesInBytes + NumSubresources);

    D3D12_RESOURCE_DESC Desc;
    ID3D12Resource_GetDesc(pDestinationResource, &Desc);
    ID3D12Device* pDevice;
    ID3D12Resource_GetDevice(pDestinationResource, &IID_ID3D12Device, &pDevice);
    ID3D12Device_GetCopyableFootprints(pDevice, &Desc, FirstSubresource,
        NumSubresources, IntermediateOffset, pLayouts, pNumRows,
        pRowSizesInBytes, &RequiredSize);
    ID3D12Device_Release(pDevice);

    UINT64 Result = UpdateSubresourcesImpl(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData);
    HeapFree(GetProcessHeap(), 0, pMem);
    return Result;
}

void InitialiseBuffer(
    ID3D12Device2* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource** pDestinationResource,
    ID3D12Resource** pIntermediateResource,
    size_t numElements, size_t elementSize, const void* bufferData,
    D3D12_RESOURCE_FLAGS flags)
{
    if (bufferData == NULL)
    {
        fprintf(stderr, "Buffer data is empty");
        return;
    }

    size_t bufferSize = numElements * elementSize;

    D3D12_HEAP_PROPERTIES dstHeapProperties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 1,
        .VisibleNodeMask = 1
    };

    D3D12_RESOURCE_DESC resourceDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = bufferSize,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {
            .Count = 1,
            .Quality = 0
        },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = flags,
    };

    // Create committed resource with an optimized GPU access that will serve
    // as the destination buffer for the data
    ExitOnFailure(ID3D12Device2_CreateCommittedResource(device, &dstHeapProperties,
        D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        NULL, &IID_ID3D12Resource, (void**)pDestinationResource));

    // Copy the heap properites from destination heap, but change the heap type
    D3D12_HEAP_PROPERTIES intermediateHeapProperties = dstHeapProperties;
    intermediateHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    // Copy the resource desc from the destination resource, but clear flags
    D3D12_RESOURCE_DESC intermediateResourceDesc = resourceDesc;
    intermediateResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // Create an intermediate buffer for transfering buffer data from CPU to GPU
    ExitOnFailure(ID3D12Device2_CreateCommittedResource(device, &intermediateHeapProperties,
        D3D12_HEAP_FLAG_NONE, &intermediateResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        NULL, &IID_ID3D12Resource, (void**)pIntermediateResource));

    D3D12_SUBRESOURCE_DATA subresourceData = {
        .pData = bufferData,
        .RowPitch = bufferSize,
        .SlicePitch = subresourceData.RowPitch
    };

    UpdateSubresources(commandList, *pDestinationResource,
        *pIntermediateResource, 0, 0, 1, &subresourceData);
}

D3D12_RESOURCE_BARRIER D3D12_RESOURCE_BARRIER_Transition(
        ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subresource, // = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_BARRIER_FLAGS flags) // = D3D12_RESOURCE_BARRIER_FLAG_NONE
{
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = flags;
    barrier.Transition.pResource = pResource;
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter = stateAfter;
    barrier.Transition.Subresource = subresource;
    return barrier;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12_CPU_DESCRIPTOR_HANDLE_Offset(
    D3D12_CPU_DESCRIPTOR_HANDLE handle,
    INT offsetInDescriptors,
    UINT descriptorIncrementSize)
{
    handle.ptr += offsetInDescriptors * descriptorIncrementSize;
    return handle;
}

uint64_t Signal(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence,
                uint64_t* fenceValue)
{
    (*fenceValue)++;
    ExitOnFailure(ID3D12CommandQueue_Signal(commandQueue, fence, *fenceValue));

    return *fenceValue;
}

HANDLE CreateEventHandle()
{
    HANDLE fenceEvent;

    fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent && "Failed to create fence event.");

    return fenceEvent;
}

void WaitForFenceValue(ID3D12Fence* fence, uint64_t fenceValue, HANDLE fenceEvent, DWORD duration)
{
    if (ID3D12Fence_GetCompletedValue(fence) < fenceValue)
    {
        ExitOnFailure(ID3D12Fence_SetEventOnCompletion(fence, fenceValue, fenceEvent));
        WaitForSingleObject(fenceEvent, duration ? duration : INFINITE);
    }
}

void LoadVertexBuffer(ID3D12Device2* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12CommandAllocator* bufferInitCommandAllocator,
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource** vertexBuffer,
    ID3D12Resource** intermediateVertexBuffer)
{
    ID3D12GraphicsCommandList_Reset(commandList, bufferInitCommandAllocator, NULL);
    InitialiseBuffer(device, commandList,
        vertexBuffer, intermediateVertexBuffer,
        _countof(g_Vertices), sizeof(Vertex), g_Vertices,
        D3D12_RESOURCE_FLAG_NONE);

    ExitOnFailure(ID3D12GraphicsCommandList_Close(commandList));

    ID3D12CommandList* const commandLists[] = { (ID3D12CommandList* const)commandList };
    ID3D12CommandQueue_ExecuteCommandLists(commandQueue, _countof(commandLists), commandLists);

    uint64_t fenceVal = 0;
    uint64_t fenceValue = Signal(commandQueue, g_Fence, &fenceVal);
    WaitForFenceValue(g_Fence, fenceValue, g_FenceEvent, 0);
}

void Update()
{
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static clock_t t0;

    frameCounter++;
    clock_t t1 = clock();
    clock_t deltaTime = t1 - t0;
    t0 = t1;

    elapsedSeconds += (double)deltaTime / CLOCKS_PER_SEC;
    if (elapsedSeconds > 1.0)
    {
        char buffer[500];
        double fps = frameCounter / elapsedSeconds;
        sprintf_s(buffer, 500, "FPS: %f\n", fps);
        OutputDebugString(buffer);

        frameCounter = 0;
        elapsedSeconds = 0.0;
    }
}

void Render(IDXGISwapChain4* swapChain, ID3D12CommandQueue* g_CommandQueue,
            ID3D12GraphicsCommandList* g_CommandList)
{
    ID3D12CommandAllocator* commandAllocator = g_CommandAllocators[g_CurrentBackBufferIndex];
    ID3D12Resource* backBuffer = g_BackBuffers[g_CurrentBackBufferIndex];

    ID3D12CommandAllocator_Reset(commandAllocator);
    ID3D12GraphicsCommandList_Reset(g_CommandList, commandAllocator, NULL);

    // Clear the render target.
    {
        D3D12_RESOURCE_BARRIER barrier = D3D12_RESOURCE_BARRIER_Transition(backBuffer,
                                                    D3D12_RESOURCE_STATE_PRESENT,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                    D3D12_RESOURCE_BARRIER_FLAG_NONE);
        ID3D12GraphicsCommandList_ResourceBarrier(g_CommandList, 1, &barrier);

        FLOAT clearColor[] = { 0.635f, 0.415f, 0.905f, 1.0f };
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_RTVDescriptorHeap, &rtv);

        rtv = D3D12_CPU_DESCRIPTOR_HANDLE_Offset(rtv, g_CurrentBackBufferIndex, g_RTVDescriptorSize);

        ID3D12GraphicsCommandList_ClearRenderTargetView(g_CommandList, rtv, clearColor, 0, NULL);
    }

    // Present
    {
        D3D12_RESOURCE_BARRIER barrier = D3D12_RESOURCE_BARRIER_Transition(backBuffer,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                    D3D12_RESOURCE_STATE_PRESENT,
                                                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                    D3D12_RESOURCE_BARRIER_FLAG_NONE);
        ID3D12GraphicsCommandList_ResourceBarrier(g_CommandList, 1, &barrier);

        ExitOnFailure(ID3D12GraphicsCommandList_Close(g_CommandList));

        ID3D12CommandList* const commandLists[] = { (ID3D12CommandList* const)g_CommandList };
        ID3D12CommandQueue_ExecuteCommandLists(g_CommandQueue, _countof(commandLists), commandLists);

        g_FrameFenceValues[g_CurrentBackBufferIndex] = Signal(g_CommandQueue, g_Fence, &g_FenceValue);

        UINT syncInterval = 1;
        UINT presentFlags = 0;
        ExitOnFailure(IDXGISwapChain4_Present(swapChain, syncInterval, presentFlags));

        g_CurrentBackBufferIndex = IDXGISwapChain4_GetCurrentBackBufferIndex(swapChain);

        WaitForFenceValue(g_Fence, g_FrameFenceValues[g_CurrentBackBufferIndex], g_FenceEvent, 0);
    }
}

void Flush(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence,
    uint64_t* fenceValue, HANDLE fenceEvent)
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent, 0);
}

int main()
{
#ifdef _DEBUG
    EnableDebuggingLayer();
#endif

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
    IDXGISwapChain4* swapChain = CreateSwapChain(hWnd, g_CommandQueue,
                                                 width, height, FRAMES_NUM);

    g_CurrentBackBufferIndex = IDXGISwapChain4_GetCurrentBackBufferIndex(swapChain);

    g_RTVDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAMES_NUM);
    g_RTVDescriptorSize = ID3D12Device2_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    UpdateRenderTargetViews(device, swapChain, g_RTVDescriptorHeap);

    for (int i = 0; i < FRAMES_NUM; ++i)
    {
        g_CommandAllocators[i] = CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }

    ID3D12CommandAllocator* bufferInitCommandAllocator = CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT);

    ID3D12GraphicsCommandList* g_CommandList = CreateCommandList(device,
        bufferInitCommandAllocator, D3D12_COMMAND_LIST_TYPE_DIRECT);

    g_Fence = CreateFence(device);
    g_FenceEvent = CreateEventHandle();

    // Vertex buffer for the cube.
    ID3D12Resource* vertexBuffer = NULL;
    ID3D12Resource* intermediateVertexBuffer = NULL;

    // Initialise the vertex buffer
    LoadVertexBuffer(device, g_CommandQueue, bufferInitCommandAllocator,
        g_CommandList, &vertexBuffer, &intermediateVertexBuffer);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    // Index buffer for the cube.
    ID3D12Resource* indexBuffer;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    // Depth buffer.
    ID3D12Resource* depthBuffer;

    // Root signature
    ID3D12RootSignature* m_RootSignature;

    // Pipeline state object.
    ID3D12PipelineState* m_PipelineState;

    D3D12_VIEWPORT m_Viewport = { 0.0f, 0.0f, (float)width, (float)height, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    D3D12_RECT m_ScissorRect = { 0, 0, LONG_MAX, LONG_MAX };

    float m_FoV = 45.0;

    mat4 m_ModelMatrix;
    mat4 m_ViewMatrix;
    mat4 m_ProjectionMatrix;

    while (!glfwWindowShouldClose(window))
    {
        Update();
        Render(swapChain, g_CommandQueue, g_CommandList);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    // Make sure the command queue has finished all commands before closing.
    Flush(g_CommandQueue, g_Fence, &g_FenceValue, g_FenceEvent);

    CloseHandle(g_FenceEvent);

    // TODO: release resources, desc heap and root sig, and pipeline state
    ID3D12Resource_Release(vertexBuffer);
    ID3D12Resource_Release(intermediateVertexBuffer);
    ID3D12Fence_Release(g_Fence);
    ID3D12GraphicsCommandList_Release(g_CommandList);
    for (int i = 0; i < FRAMES_NUM; ++i)
    {
        ID3D12CommandAllocator_Release(g_CommandAllocators[i]);
    }
    ID3D12CommandAllocator_Release(bufferInitCommandAllocator);
    ID3D12DescriptorHeap_Release(g_RTVDescriptorHeap);
    IDXGISwapChain4_Release(swapChain);
    ID3D12CommandQueue_Release(g_CommandQueue);
    ID3D12Device2_Release(device);
    IDXGIAdapter4_Release(dxgiAdapter4);

    return 0;
}