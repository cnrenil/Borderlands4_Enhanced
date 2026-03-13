#include "pch.h"
#include "D3D12Hook.h"
#include "GUI/Menu.h"
#include "Engine.h"
#include "Config/ConfigManager.h"
#include "Cheats.h"
#include "Utils/Hotkey.h"

extern WNDPROC oWndProc;
extern HWND g_hWnd;
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

    // --- Stability: track resize state and early-injection grace period ---
    static DWORD                  gFirstPresentTime = 0;
    static bool                   gFirstPresentSeen = false;
    // Grace period (ms) after first Present before we initialize ImGui.
    // This lets the game finish HDR / DLSS / swapchain setup.
    static constexpr DWORD        INIT_GRACE_PERIOD_MS = 5000;

    inline void LogHRESULT(const char* label, HRESULT hr) {
        printf("[d3d12hook] %s: hr=0x%08X\n", label, hr);
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

        // ImGui Setup
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        // Load Font and Build Atlas (CRITICAL FIX)
        char winDir[MAX_PATH];
        GetWindowsDirectoryA(winDir, MAX_PATH);
        std::string fontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";
        if (GetFileAttributesA(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
        } else {
            io.Fonts->AddFontDefault();
        }
        io.Fonts->Build(); // Ensure font atlas is built before backend init

        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hWnd);
        ImGui_ImplDX12_Init(gDevice, gBufferCount, desc.BufferDesc.Format, gHeapSRV,
            gHeapSRV->GetCPUDescriptorHandleForHeapStart(),
            gHeapSRV->GetGPUDescriptorHandleForHeapStart());

        // Sync Objects
        gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gOverlayFence));
        gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        gOverlayFenceValue = 0;
        
        gInitialized = true;
        printf("[d3d12hook] Success: ImGui and DX12 resources fully initialized!\n");
    }

    void RenderImGui(IDXGISwapChain3* pSwapChain) {
        // Don't render while a resize is in progress
        if (Resizing.load()) return;

        static uint64_t last_rendered_frame = 0;
        uint64_t current_time = GetTickCount64();
        
        static UINT last_index = 0xFFFFFFFF;
        UINT current_index = pSwapChain->GetCurrentBackBufferIndex();
        
        if (current_index == last_index && (current_time - last_rendered_frame < 5)) 
            return;

        last_rendered_frame = current_time;
        last_index = current_index;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        try {
            // All rendering logic is now consolidated into Cheats::Render()
            Cheats::Render();
        } catch (...) {}

        ImGui::Render();

        UINT frameIdx = pSwapChain->GetCurrentBackBufferIndex();
        if (frameIdx >= gBufferCount) return; // Safety check
        FrameContext& ctx = gFrameContexts[frameIdx];
        if (!ctx.renderTarget || !ctx.allocator) return; // Safety check

        // Wait for fence with a bounded timeout to avoid hangs
        if (gOverlayFence && gOverlayFence->GetCompletedValue() < gOverlayFenceValue) {
            if (SUCCEEDED(gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent))) {
                DWORD waitResult = WaitForSingleObject(gFenceEvent, 1000);
                if (waitResult == WAIT_TIMEOUT) {
                    printf("[d3d12hook] WARNING: Fence wait timed out, skipping frame.\n");
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
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = ctx.renderTarget;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        gCommandList->ResourceBarrier(1, &barrier);

        gCommandList->OMSetRenderTargets(1, &ctx.rtvHandle, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[] = { gHeapSRV };
        gCommandList->SetDescriptorHeaps(1, heaps);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        gCommandList->ResourceBarrier(1, &barrier);
        gCommandList->Close();

        ID3D12CommandList* ppCommandLists[] = { gCommandList };
        oExecuteCommandListsD3D12(gCommandQueue, 1, ppCommandLists);
        gCommandQueue->Signal(gOverlayFence, ++gOverlayFenceValue);
    }

    long __stdcall hookPresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (gShutdown) return oPresentD3D12(pSwapChain, SyncInterval, Flags);

        // Track first Present time for grace period
        if (!gFirstPresentSeen) {
            gFirstPresentSeen = true;
            gFirstPresentTime = GetTickCount();
            printf("[d3d12hook] First Present detected, starting %dms grace period for game init...\n", INIT_GRACE_PERIOD_MS);
        }

        gAfterFirstPresent = true;
        if (!gCommandQueue) return oPresentD3D12(pSwapChain, SyncInterval, Flags);

        // Don't initialize during the grace period — let the game finish
        // HDR / DLSS / swapchain setup first.
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
            Logger::LogThrottled(Logger::Level::Info, "D3D12", 5000, "hookPresentD3D12: Waiting for ImGui Init (Grace Period)...");
        }

        return oPresentD3D12(pSwapChain, SyncInterval, Flags);
    }

    long __stdcall hookPresent1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams) {
        if (gShutdown) return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);

        // Track first Present time for grace period
        if (!gFirstPresentSeen) {
            gFirstPresentSeen = true;
            gFirstPresentTime = GetTickCount();
            printf("[d3d12hook] First Present1 detected, starting %dms grace period for game init...\n", INIT_GRACE_PERIOD_MS);
        }

        gAfterFirstPresent = true;
        if (!gCommandQueue) return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);

        // Don't initialize during the grace period
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

        return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
    }

    void __stdcall hookExecuteCommandListsD3D12(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
        if (gShutdown) return oExecuteCommandListsD3D12(_this, NumCommandLists, ppCommandLists);

        if (!gCommandQueue && gAfterFirstPresent && _this) {
            D3D12_COMMAND_QUEUE_DESC desc = _this->GetDesc();
            if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                gCommandQueue = _this;
                printf("[d3d12hook] Success: Captured Direct CommandQueue at ExecuteCommandLists.\n");
            }
        }
        gAfterFirstPresent = false;
        if (oExecuteCommandListsD3D12)
            oExecuteCommandListsD3D12(_this, NumCommandLists, ppCommandLists);
    }

    HRESULT __stdcall hookResizeBuffersD3D12(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        printf("[d3d12hook] ResizeBuffers detected (%ux%u, fmt=%d), cleaning up...\n", Width, Height, (int)NewFormat);
        
        // Signal that we're resizing — Present hook will skip rendering
        Resizing.store(true);

        if (gInitialized) {
            // Shut down ImGui backends
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();

            // Release all our GPU resources
            ReleaseOverlayResources();

            gInitialized = false;
        }

        // Call the original ResizeBuffers
        HRESULT hr = oResizeBuffersD3D12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        // Clear resize flag — the next Present call will re-initialize
        Resizing.store(false);

        printf("[d3d12hook] ResizeBuffers completed (hr=0x%08X). Will re-init on next Present.\n", hr);
        return hr;
    }

    void release() {
        if (gShutdown) return;
        gShutdown = true;
        
        // Let ongoing frames finish or skip
        Sleep(100);

        if (gInitialized) {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();

            ReleaseOverlayResources();
            gInitialized = false;
        }

        if (oWndProc && g_hWnd) {
            SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        }

        MH_DisableHook(MH_ALL_HOOKS);
        // We leave MH_Uninitialize to the main shutdown thread to avoid deadlocks
    }
}
