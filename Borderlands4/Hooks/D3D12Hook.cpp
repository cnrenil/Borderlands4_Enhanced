#include "pch.h"

extern WNDPROC oWndProc;
extern HWND g_hWnd;
extern HWND g_hTrackedWindow;
extern LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern std::atomic<int> g_PresentCount;
extern std::atomic<bool> Resizing;

namespace d3d12hook {
    PresentD3D12            oPresentD3D12 = nullptr;
    Present1Fn              oPresent1D3D12 = nullptr;
    ExecuteCommandListsFn   oExecuteCommandListsD3D12 = nullptr;
    ResizeBuffersFn         oResizeBuffersD3D12 = nullptr;

    static ID3D12Device* gDevice = nullptr;
    static ID3D12CommandQueue* gCommandQueue = nullptr;
    static ID3D12DescriptorHeap* gHeapRTV = nullptr;
    static ID3D12DescriptorHeap* gHeapSRV = nullptr;
    static ID3D12GraphicsCommandList* gCommandList = nullptr;
    static ID3D12Fence* gOverlayFence = nullptr;
    static HANDLE                   gFenceEvent = nullptr;
    static UINT64                  gOverlayFenceValue = 0;
    static UINT                    gBufferCount = 0;

    struct FrameContext {
        ID3D12CommandAllocator* allocator;
        ID3D12Resource* renderTarget;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    };
    static FrameContext* gFrameContexts = nullptr;
    static bool                   gInitialized = false;
    static bool                   gShutdown = false;
    static bool                   gAfterFirstPresent = false;
    static IDXGISwapChain3*       gTrackedSwapChain = nullptr;
    static bool                   gNeedQueueRecapture = true;
    static DXGI_FORMAT            gSwapChainFormat = DXGI_FORMAT_UNKNOWN;
    static bool                   gSwapChainWasHdr = false;
    static DXGI_FORMAT            gLastLoggedFormat = DXGI_FORMAT_UNKNOWN;
    static bool                   gLastLoggedHdr = false;
    static std::mutex             gOverlayMutex;

    // --- Stability: track resize state and early-injection grace period ---
    static DWORD                  gFirstPresentTime = 0;
    static bool                   gFirstPresentSeen = false;
    // Grace period (ms) after first Present before we initialize ImGui.
    // This lets the game finish HDR / DLSS / swapchain setup.
    static constexpr DWORD        INIT_GRACE_PERIOD_MS = 5000;
    // Extra cooldown after each ResizeBuffers.
    // Keep it short; set to 0 to disable completely.
    static constexpr DWORD        POST_RESIZE_COOLDOWN_MS = 500;
    static DWORD                  gLastResizeTime = 0;

    [[noreturn]] static void ForceExitOnGameShutdown(const char* reason) {
        if (reason) {
            LOG_INFO("DX12Hook", "Shutdown detected in D3D12 hook path: %s. Exiting process.\n", reason);
        }
        gShutdown = true;
        ExitProcess(0);
    }

    static long CallOriginalPresentSEH(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        __try {
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ForceExitOnGameShutdown("Present/original call fault");
        }
    }

    static long CallOriginalPresent1SEH(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams)
    {
        __try {
            return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ForceExitOnGameShutdown("Present1/original call fault");
        }
    }

    static void CallOriginalExecuteCommandListsSEH(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
    {
        __try {
            oExecuteCommandListsD3D12(queue, NumCommandLists, ppCommandLists);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ForceExitOnGameShutdown("ExecuteCommandLists/original call fault");
        }
    }

    static bool IsGameWindowGone() {
        return g_hWnd && !IsWindow(g_hWnd);
    }

    static bool IsWindowRenderable()
    {
        if (!g_hWnd || !IsWindow(g_hWnd))
            return false;

        if (IsIconic(g_hWnd))
            return false;

        RECT rect{};
        if (!GetClientRect(g_hWnd, &rect))
            return false;

        return rect.right > rect.left && rect.bottom > rect.top;
    }

    static bool IsInPostResizeCooldown() {
        if (gLastResizeTime == 0) return false;
        return (GetTickCount() - gLastResizeTime) < POST_RESIZE_COOLDOWN_MS;
    }

    static void ReleaseCapturedQueue() {
        if (gCommandQueue) {
            gCommandQueue->Release();
            gCommandQueue = nullptr;
        }
    }

    static void CaptureQueue(ID3D12CommandQueue* queue) {
        if (!queue || queue == gCommandQueue) return;
        queue->AddRef();
        ReleaseCapturedQueue();
        gCommandQueue = queue;
    }

    static void ResetStartupTracking(const char* reason) {
        gFirstPresentSeen = false;
        gFirstPresentTime = 0;
        gAfterFirstPresent = false;
        gNeedQueueRecapture = true;
        gSwapChainFormat = DXGI_FORMAT_UNKNOWN;
        gSwapChainWasHdr = false;
        gLastLoggedFormat = DXGI_FORMAT_UNKNOWN;
        gLastLoggedHdr = false;
        ReleaseCapturedQueue();
        if (reason) {
            LOG_DEBUG("DX12Hook", "Startup tracking reset: %s\n", reason);
        }
    }

    inline void LogHRESULT(const char* label, HRESULT hr) {
        LOG_ERROR("DX12Hook", "%s: hr=0x%08X\n", label, hr);
    }

    static bool IsHdrFormat(DXGI_FORMAT format) {
        return format == DXGI_FORMAT_R10G10B10A2_UNORM ||
            format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    static void RefreshSwapChainColorState(IDXGISwapChain3* pSwapChain) {
        if (!pSwapChain) return;

        DXGI_SWAP_CHAIN_DESC desc = {};
        if (SUCCEEDED(pSwapChain->GetDesc(&desc))) {
            gSwapChainFormat = desc.BufferDesc.Format;
        }
        gSwapChainWasHdr = IsHdrFormat(gSwapChainFormat);
    }

    // Release all overlay GPU resources (but NOT the ImGui context itself).
    static void ReleaseOverlayResources() {
        // Wait for GPU to finish our overlay work before releasing
        if (gOverlayFence && gCommandQueue && gFenceEvent) {
            UINT64 completed = gOverlayFence->GetCompletedValue();
            if (completed < gOverlayFenceValue) {
                gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent);
                WaitForSingleObject(gFenceEvent, 2000); // bounded wait
            }
        }

        if (gCommandList) { gCommandList->Release(); gCommandList = nullptr; }
        if (gOverlayFence) { gOverlayFence->Release(); gOverlayFence = nullptr; }
        if (gFenceEvent) { CloseHandle(gFenceEvent); gFenceEvent = nullptr; }
        gOverlayFenceValue = 0;

        if (gFrameContexts) {
            for (UINT i = 0; i < gBufferCount; ++i) {
                if (gFrameContexts[i].renderTarget) { gFrameContexts[i].renderTarget->Release(); gFrameContexts[i].renderTarget = nullptr; }
                if (gFrameContexts[i].allocator) { gFrameContexts[i].allocator->Release(); gFrameContexts[i].allocator = nullptr; }
            }
            delete[] gFrameContexts;
            gFrameContexts = nullptr;
        }

        if (gHeapRTV) { gHeapRTV->Release(); gHeapRTV = nullptr; }
        if (gHeapSRV) { gHeapSRV->Release(); gHeapSRV = nullptr; }
        if (gDevice) { gDevice->Release(); gDevice = nullptr; }
    }

    void InitImGuiAndResources(IDXGISwapChain3* pSwapChain) {
        if (gInitialized) return;

        if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&gDevice))) {
            LogHRESULT("GetDevice Failed", E_FAIL);
            return;
        }

        DXGI_SWAP_CHAIN_DESC desc = {};
        pSwapChain->GetDesc(&desc);
        gBufferCount = desc.BufferCount;
        g_hWnd = desc.OutputWindow;
        if (g_hWnd)
            g_hTrackedWindow = g_hWnd;
        gSwapChainFormat = desc.BufferDesc.Format;

        // Hook WndProc for Input
        if (g_hWnd && !oWndProc) {
            oWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
        }

        // Create RTV Heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = gBufferCount;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(gDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&gHeapRTV)))) {
            gDevice->Release(); gDevice = nullptr;
            return;
        }

        // Create SRV Heap (for ImGui)
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.NumDescriptors = 100; 
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(gDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&gHeapSRV)))) {
            gHeapRTV->Release(); gHeapRTV = nullptr;
            gDevice->Release(); gDevice = nullptr;
            return;
        }

        // Create Frame Contexts
        gFrameContexts = new FrameContext[gBufferCount]();
        for (UINT i = 0; i < gBufferCount; ++i) {
            if (FAILED(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gFrameContexts[i].allocator)))) {
                // Cleanup on failure
                ReleaseOverlayResources();
                return;
            }
            
            ID3D12Resource* back = nullptr;
            if (SUCCEEDED(pSwapChain->GetBuffer(i, IID_PPV_ARGS(&back)))) {
                gFrameContexts[i].renderTarget = back;
            }
        }

        // Initialize Handles
        UINT rtvSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gHeapRTV->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < gBufferCount; ++i) {
            gFrameContexts[i].rtvHandle = rtvHandle;
            gDevice->CreateRenderTargetView(gFrameContexts[i].renderTarget, nullptr, rtvHandle);
            rtvHandle.ptr += rtvSize;
        }

        if (!GUI::Overlay::Initialize(g_hWnd, gDevice, gBufferCount, desc.BufferDesc.Format, gHeapSRV))
        {
            ReleaseOverlayResources();
            return;
        }

        RECT clientRect{};
        GetClientRect(g_hWnd, &clientRect);
        GUI::BackdropBlur::Initialize(
            gDevice,
            gHeapSRV,
            desc.BufferDesc.Format,
            static_cast<UINT>((std::max)(clientRect.right - clientRect.left, 1L)),
            static_cast<UINT>((std::max)(clientRect.bottom - clientRect.top, 1L)));

        // Sync Objects
        gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gOverlayFence));
        gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        gOverlayFenceValue = 0;
        
        gInitialized = true;
        LOG_DEBUG("DX12Hook","ImGui and DX12 resources fully initialized!\n");
    }

    void RenderImGui(IDXGISwapChain3* pSwapChain) {
        // Don't render while a resize is in progress
        if (Resizing.load()) return;
        if (!IsWindowRenderable()) return;
        if (!gCommandQueue || !oExecuteCommandListsD3D12) return;

        static uint64_t last_rendered_frame = 0;
        uint64_t current_time = GetTickCount64();
        
        static UINT last_index = 0xFFFFFFFF;
        UINT current_index = pSwapChain->GetCurrentBackBufferIndex();
        
        if (current_index == last_index && (current_time - last_rendered_frame < 5)) 
            return;

        last_rendered_frame = current_time;
        last_index = current_index;

        GUI::Overlay::BeginFrame();
        GUI::Overlay::BuildFrame();
        ImDrawData* drawData = GUI::Overlay::GetDrawData();
        if (!drawData) return;

        UINT frameIdx = pSwapChain->GetCurrentBackBufferIndex();
        if (frameIdx >= gBufferCount) return; // Safety check
        FrameContext& ctx = gFrameContexts[frameIdx];
        if (!ctx.renderTarget || !ctx.allocator) return; // Safety check

        // Wait for fence with a bounded timeout to avoid hangs
        if (gOverlayFence && gOverlayFence->GetCompletedValue() < gOverlayFenceValue) {
            if (SUCCEEDED(gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent))) {
                DWORD waitResult = WaitForSingleObject(gFenceEvent, 1000);
                if (waitResult == WAIT_TIMEOUT) {
                    LOG_WARN("DX12Hook","Fence wait timed out, skipping frame.\n");
                    return; // Skip this frame rather than hang
                }
            }
        }

        HRESULT hr = ctx.allocator->Reset();
        if (FAILED(hr)) return; // Allocator reset failed, skip frame

        if (!gCommandList) {
            hr = gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.allocator, nullptr, IID_PPV_ARGS(&gCommandList));
            if (FAILED(hr)) return;
            gCommandList->Close();
        }
        hr = gCommandList->Reset(ctx.allocator, nullptr);
        if (FAILED(hr)) return;

        D3D12_RESOURCE_BARRIER barrier = {};
        const GUI::MenuBackdropState& backdropState = GUI::GetMenuBackdropState();
        if (backdropState.Visible)
        {
            RECT clientRect{};
            GetClientRect(g_hWnd, &clientRect);
            GUI::BackdropBlur::Resize(
                static_cast<UINT>((std::max)(clientRect.right - clientRect.left, 1L)),
                static_cast<UINT>((std::max)(clientRect.bottom - clientRect.top, 1L)),
                gSwapChainFormat);
            GUI::BackdropBlur::Render(gCommandList, ctx.renderTarget, ctx.rtvHandle, backdropState);
        }
        else
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = ctx.renderTarget;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            gCommandList->ResourceBarrier(1, &barrier);
        }

        gCommandList->OMSetRenderTargets(1, &ctx.rtvHandle, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[] = { gHeapSRV };
        gCommandList->SetDescriptorHeaps(1, heaps);
        if (drawData->CmdListsCount > 0 && drawData->TotalVtxCount > 0)
            ImGui_ImplDX12_RenderDrawData(drawData, gCommandList);

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = ctx.renderTarget;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        gCommandList->ResourceBarrier(1, &barrier);
        gCommandList->Close();

        ID3D12CommandList* ppCommandLists[] = { gCommandList };
        CallOriginalExecuteCommandListsSEH(gCommandQueue, 1, ppCommandLists);
        if (gCommandQueue && gOverlayFence)
            gCommandQueue->Signal(gOverlayFence, ++gOverlayFenceValue);
    }

    long __stdcall hookPresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (IsGameWindowGone()) ForceExitOnGameShutdown("Present/window invalid");
        if (gShutdown) return oPresentD3D12(pSwapChain, SyncInterval, Flags);

        if (IsInPostResizeCooldown()) {
            gAfterFirstPresent = false;
            Logger::LogThrottled(Logger::Level::Debug, "DX12Hook", 2000, "Present: in post-resize cooldown, forwarding without overlay init");
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }

        if (pSwapChain != gTrackedSwapChain) {
            gTrackedSwapChain = pSwapChain;
            ResetStartupTracking("SwapChain changed");
        }

        RefreshSwapChainColorState(pSwapChain);
        if (gSwapChainFormat != gLastLoggedFormat || gSwapChainWasHdr != gLastLoggedHdr) {
            if (gSwapChainWasHdr) {
                Logger::LogThrottled(
                    Logger::Level::Debug,
                    "DX12Hook",
                    2000,
                    "HDR swapchain detected. Leaving game colorspace unchanged to avoid SDR washout. fmt=%d",
                    static_cast<int>(gSwapChainFormat));
            }
            gLastLoggedFormat = gSwapChainFormat;
            gLastLoggedHdr = gSwapChainWasHdr;
        }

        // Track first Present time for grace period
        if (!gFirstPresentSeen) {
            gFirstPresentSeen = true;
            gFirstPresentTime = GetTickCount();
            LOG_DEBUG("DX12Hook","First Present detected, starting %dms grace period for game init...\n", INIT_GRACE_PERIOD_MS);
        }

        gAfterFirstPresent = true;
        if (!gCommandQueue) return oPresentD3D12(pSwapChain, SyncInterval, Flags);

        // Don't initialize during the grace period — let the game finish
        // HDR / DLSS / swapchain setup first.
        {
            std::scoped_lock overlayLock(gOverlayMutex);
            if (!gInitialized) {
                DWORD elapsed = GetTickCount() - gFirstPresentTime;
                if (elapsed < INIT_GRACE_PERIOD_MS) {
                    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
                }
                InitImGuiAndResources(pSwapChain);
            }

            if (gInitialized && !Resizing.load()) {
                RenderImGui(pSwapChain);
                g_PresentCount.fetch_add(1);
            }
            else if (!gInitialized)
            {
                    Logger::LogThrottled(Logger::Level::Debug, "D3D12", 5000, "hookPresentD3D12: Waiting for ImGui Init (Grace Period)...");
            }
        }

        return CallOriginalPresentSEH(pSwapChain, SyncInterval, Flags);
    }

    long __stdcall hookPresent1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams) {
        if (IsGameWindowGone()) ForceExitOnGameShutdown("Present1/window invalid");
        if (gShutdown) return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);

        if (IsInPostResizeCooldown()) {
            gAfterFirstPresent = false;
            Logger::LogThrottled(Logger::Level::Debug, "DX12Hook", 2000, "Present1: in post-resize cooldown, forwarding without overlay init");
            return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
        }

        if (pSwapChain != gTrackedSwapChain) {
            gTrackedSwapChain = pSwapChain;
            ResetStartupTracking("SwapChain changed (Present1)");
        }

        RefreshSwapChainColorState(pSwapChain);
        if (gSwapChainFormat != gLastLoggedFormat || gSwapChainWasHdr != gLastLoggedHdr) {
            if (gSwapChainWasHdr) {
                Logger::LogThrottled(
                    Logger::Level::Debug,
                    "DX12Hook",
                    2000,
                    "HDR swapchain detected on Present1. Leaving game colorspace unchanged to avoid SDR washout. fmt=%d",
                    static_cast<int>(gSwapChainFormat));
            }
            gLastLoggedFormat = gSwapChainFormat;
            gLastLoggedHdr = gSwapChainWasHdr;
        }

        // Track first Present time for grace period
        if (!gFirstPresentSeen) {
            gFirstPresentSeen = true;
            gFirstPresentTime = GetTickCount();
            LOG_DEBUG("DX12Hook", "First Present1 detected, starting %dms grace period for game init...\n", INIT_GRACE_PERIOD_MS);
        }

        gAfterFirstPresent = true;
        if (!gCommandQueue) return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);

        // Don't initialize during the grace period
        {
            std::scoped_lock overlayLock(gOverlayMutex);
            if (!gInitialized) {
                DWORD elapsed = GetTickCount() - gFirstPresentTime;
                if (elapsed < INIT_GRACE_PERIOD_MS) {
                    return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
                }
                InitImGuiAndResources(pSwapChain);
            }

            if (gInitialized && !Resizing.load()) {
                RenderImGui(pSwapChain);
                g_PresentCount.fetch_add(1);
            }
        }

        return CallOriginalPresent1SEH(pSwapChain, SyncInterval, Flags, pParams);
    }

    void __stdcall hookExecuteCommandListsD3D12(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
        if (IsGameWindowGone()) ForceExitOnGameShutdown("ExecuteCommandLists/window invalid");
        if (gShutdown) return oExecuteCommandListsD3D12(_this, NumCommandLists, ppCommandLists);
        if (IsInPostResizeCooldown()) {
            gAfterFirstPresent = false;
            return oExecuteCommandListsD3D12(_this, NumCommandLists, ppCommandLists);
        }

        if (_this && (gNeedQueueRecapture || !gCommandQueue) && gAfterFirstPresent) {
            D3D12_COMMAND_QUEUE_DESC desc = _this->GetDesc();
            if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                CaptureQueue(_this);
                gNeedQueueRecapture = false;
                LOG_DEBUG("DX12Hook", "Success: Captured Direct CommandQueue at ExecuteCommandLists.\n");
            }
        }
        gAfterFirstPresent = false;
        if (oExecuteCommandListsD3D12)
            CallOriginalExecuteCommandListsSEH(_this, NumCommandLists, ppCommandLists);
    }

    HRESULT __stdcall hookResizeBuffersD3D12(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        LOG_DEBUG("DX12Hook", "ResizeBuffers detected (%ux%u, fmt=%d), cleaning up...\n", Width, Height, (int)NewFormat);
        gLastResizeTime = GetTickCount();
        
        // Signal that we're resizing — Present hook will skip rendering
        Resizing.store(true);

        HRESULT hr = S_OK;
        {
            std::scoped_lock overlayLock(gOverlayMutex);

            if (gInitialized) {
                GUI::BackdropBlur::Shutdown();
                GUI::Overlay::Shutdown();

                // Release all our GPU resources
                ReleaseOverlayResources();

                gInitialized = false;
            }

            if (oWndProc && g_hWnd) {
                SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
                oWndProc = nullptr;
            }
            g_hWnd = nullptr;

            // Call the original ResizeBuffers while overlay resources stay quiesced.
            hr = oResizeBuffersD3D12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        }

        // Force recapture after swapchain reconfiguration to avoid stale queue usage.
        ResetStartupTracking("ResizeBuffers");

        // Clear resize flag — the next Present call will re-initialize
        Resizing.store(false);

        LOG_DEBUG("DX12Hook", "ResizeBuffers completed (hr=0x%08X). Will re-init on next Present.\n", hr);
        return hr;
    }

    void release() {
        if (gShutdown) return;
        gShutdown = true;
        
        // Let ongoing frames finish or skip
        Sleep(100);

        {
            std::scoped_lock overlayLock(gOverlayMutex);

            if (gInitialized) {
                GUI::BackdropBlur::Shutdown();
                GUI::Overlay::Shutdown();

                ReleaseOverlayResources();
                gInitialized = false;
            }

            ReleaseCapturedQueue();
            gTrackedSwapChain = nullptr;

            if (oWndProc && g_hWnd) {
                SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
                oWndProc = nullptr;
            }
            g_hWnd = nullptr;
        }

        MH_DisableHook(MH_ALL_HOOKS);
        // We leave MH_Uninitialize to the main shutdown thread to avoid deadlocks
    }
}
