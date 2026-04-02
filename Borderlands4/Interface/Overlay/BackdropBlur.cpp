#include "pch.h"

namespace GUI::BackdropBlur
{
    namespace
    {
        constexpr UINT kMaxLevels = 4;
        constexpr UINT kSrvHeapBaseIndex = 48;

        struct BlurLevel
        {
            ID3D12Resource* Resource = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu{};
            D3D12_CPU_DESCRIPTOR_HANDLE RtvCpu{};
            UINT Width = 0;
            UINT Height = 0;
        };

        struct ShaderConstants
        {
            float Values[24]{};
        };

        ID3D12Device* gDevice = nullptr;
        ID3D12DescriptorHeap* gSharedSrvHeap = nullptr;
        ID3D12DescriptorHeap* gBlurRtvHeap = nullptr;
        ID3D12RootSignature* gRootSignature = nullptr;
        ID3D12PipelineState* gBlurPipeline = nullptr;
        ID3D12PipelineState* gCompositePipeline = nullptr;
        ID3D12Resource* gSceneCopy = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE gSceneCopySrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gSceneCopySrvGpu{};
        UINT gSrvIncrement = 0;
        UINT gRtvIncrement = 0;
        UINT gWidth = 0;
        UINT gHeight = 0;
        DXGI_FORMAT gFormat = DXGI_FORMAT_UNKNOWN;
        std::array<BlurLevel, kMaxLevels> gLevels{};
        std::vector<GlowRect> gGlowRects;

        D3D12_CPU_DESCRIPTOR_HANDLE CpuSrvAt(UINT index)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE handle = gSharedSrvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) * gSrvIncrement;
            return handle;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GpuSrvAt(UINT index)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE handle = gSharedSrvHeap->GetGPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<UINT64>(index) * gSrvIncrement;
            return handle;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE RtvAt(UINT index)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE handle = gBlurRtvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) * gRtvIncrement;
            return handle;
        }

        void ReleaseResource(ID3D12Resource*& resource)
        {
            if (resource)
            {
                resource->Release();
                resource = nullptr;
            }
        }

        void ReleasePipeline()
        {
            if (gCompositePipeline) { gCompositePipeline->Release(); gCompositePipeline = nullptr; }
            if (gBlurPipeline) { gBlurPipeline->Release(); gBlurPipeline = nullptr; }
            if (gRootSignature) { gRootSignature->Release(); gRootSignature = nullptr; }
        }

        void ReleaseTextures()
        {
            ReleaseResource(gSceneCopy);
            for (BlurLevel& level : gLevels)
            {
                ReleaseResource(level.Resource);
                level = {};
            }
        }

        HRESULT CompileShader(const char* source, const char* entryPoint, const char* profile, ID3DBlob** blob)
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if BL4_DEBUG_BUILD
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
            ID3DBlob* errors = nullptr;
            const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, profile, flags, 0, blob, &errors);
            if (errors)
            {
                LOG_ERROR("BackdropBlur", "Shader compile log: %s\n", static_cast<const char*>(errors->GetBufferPointer()));
                errors->Release();
            }
            return hr;
        }

        bool CreatePipelines()
        {
            static const char* kShaderSource = R"(
Texture2D SceneTex : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer BlurConstants : register(b0)
{
    float4 C0;
    float4 C1;
    float4 C2;
    float4 C3;
    float4 C4;
    float4 C5;
}

struct VSOut
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut output;
    float2 pos = float2((vertexId == 2) ? 3.0 : -1.0, (vertexId == 1) ? 3.0 : -1.0);
    output.Position = float4(pos, 0.0, 1.0);
    output.UV = float2((pos.x + 1.0) * 0.5, 1.0 - ((pos.y + 1.0) * 0.5));
    return output;
}

float4 BlurPS(VSOut input) : SV_Target0
{
    float2 texel = C0.xy;
    float radius = C0.z;
    float2 diag = texel * radius;
    float2 axis = texel * (radius * 2.0);
    float4 color = SceneTex.Sample(LinearSampler, input.UV) * 4.0;
    color += SceneTex.Sample(LinearSampler, input.UV + float2(diag.x, diag.y));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(-diag.x, diag.y));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(diag.x, -diag.y));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(-diag.x, -diag.y));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(axis.x, 0.0));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(-axis.x, 0.0));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(0.0, axis.y));
    color += SceneTex.Sample(LinearSampler, input.UV + float2(0.0, -axis.y));
    return color / 12.0;
}

float RoundedBoxSdf(float2 p, float2 center, float2 halfSize, float radius)
{
    float2 q = abs(p - center) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 CompositePS(VSOut input) : SV_Target0
{
    float4 rect = C0;
    float4 accent = C1;
    float4 tint = C2;
    float4 misc = C3;
    float4 glow = C4;
    float4 fx = C5;
    float mainPass = step(0.5, accent.a);

    float2 pixelPos = float2(input.UV.x * misc.z, input.UV.y * misc.w);
    float2 center = (rect.xy + rect.zw) * 0.5;
    float2 halfSize = (rect.zw - rect.xy) * 0.5;
    float sdf = RoundedBoxSdf(pixelPos, center, halfSize, misc.x);
    float mask = saturate(1.0 - smoothstep(0.0, 2.5, sdf));
    float border = saturate(1.0 - smoothstep(1.0, 3.5, abs(sdf)));
    float outside = max(sdf, 0.0);

    float shadowSdf = RoundedBoxSdf(pixelPos, center + float2(0.0, 12.0), halfSize, misc.x);
    float shadowOutside = max(shadowSdf, 0.0);
    float shadowAlpha = exp(-shadowOutside / max(fx.z, 0.001)) * fx.y * (1.0 - mask) * mainPass;
    float glowAlpha = exp(-outside / max(fx.x, 0.001)) * fx.w * (1.0 - mask);
    float4 clipRect = tint;
    float clipMask = 1.0;
    if (mainPass < 0.5)
    {
        clipMask *= step(clipRect.x, pixelPos.x);
        clipMask *= step(clipRect.y, pixelPos.y);
        clipMask *= step(pixelPos.x, clipRect.z);
        clipMask *= step(pixelPos.y, clipRect.w);
        glowAlpha *= clipMask;
    }

    float4 blurred = SceneTex.Sample(LinearSampler, input.UV);
    float3 glass = lerp(blurred.rgb, tint.rgb, tint.a);
    glass += accent.rgb * (0.08 + border * 0.20) * mainPass;
    float glassAlpha = mask * misc.y * mainPass;
    float borderAlpha = border * 0.22 * mask * mainPass;
    float3 shadowColor = float3(0.02, 0.03, 0.05);
    float3 color = shadowColor * shadowAlpha;
    color += glow.rgb * glowAlpha;
    color += glass * glassAlpha;
    color += accent.rgb * border * 0.12 * mainPass;
    float alpha = saturate(shadowAlpha + glowAlpha * 0.72 + glassAlpha + borderAlpha);

    return float4(color, alpha);
}
)";

            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 0;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = 0;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[0].DescriptorTable.NumDescriptorRanges = 1;
            params[0].DescriptorTable.pDescriptorRanges = &range;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].Constants.ShaderRegister = 0;
            params[1].Constants.RegisterSpace = 0;
            params[1].Constants.Num32BitValues = 24;

            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ShaderRegister = 0;
            sampler.RegisterSpace = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;

            D3D12_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.NumParameters = static_cast<UINT>(std::size(params));
            rootDesc.pParameters = params;
            rootDesc.NumStaticSamplers = 1;
            rootDesc.pStaticSamplers = &sampler;
            rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ID3DBlob* rootBlob = nullptr;
            ID3DBlob* errorBlob = nullptr;
            if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob)))
            {
                if (errorBlob)
                {
                    LOG_ERROR("BackdropBlur", "Root signature error: %s\n", static_cast<const char*>(errorBlob->GetBufferPointer()));
                    errorBlob->Release();
                }
                return false;
            }

            const HRESULT rootHr = gDevice->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&gRootSignature));
            rootBlob->Release();
            if (FAILED(rootHr))
                return false;

            ID3DBlob* vertexBlob = nullptr;
            ID3DBlob* blurBlob = nullptr;
            ID3DBlob* compositeBlob = nullptr;
            if (FAILED(CompileShader(kShaderSource, "VSMain", "vs_5_0", &vertexBlob)) ||
                FAILED(CompileShader(kShaderSource, "BlurPS", "ps_5_0", &blurBlob)) ||
                FAILED(CompileShader(kShaderSource, "CompositePS", "ps_5_0", &compositeBlob)))
            {
                if (vertexBlob) vertexBlob->Release();
                if (blurBlob) blurBlob->Release();
                if (compositeBlob) compositeBlob->Release();
                return false;
            }

            D3D12_BLEND_DESC opaqueBlend{};
            opaqueBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            D3D12_RASTERIZER_DESC rasterizer{};
            rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
            rasterizer.CullMode = D3D12_CULL_MODE_NONE;
            rasterizer.DepthClipEnable = TRUE;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = gRootSignature;
            desc.VS = { vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize() };
            desc.PS = { blurBlob->GetBufferPointer(), blurBlob->GetBufferSize() };
            desc.BlendState = opaqueBlend;
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = rasterizer;
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = gFormat;
            desc.SampleDesc.Count = 1;

            HRESULT hr = gDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&gBlurPipeline));
            if (SUCCEEDED(hr))
            {
                D3D12_BLEND_DESC alphaBlend = opaqueBlend;
                alphaBlend.RenderTarget[0].BlendEnable = TRUE;
                alphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                alphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                alphaBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                alphaBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                alphaBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                alphaBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                alphaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                desc.BlendState = alphaBlend;
                desc.PS = { compositeBlob->GetBufferPointer(), compositeBlob->GetBufferSize() };
                hr = gDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&gCompositePipeline));
            }

            vertexBlob->Release();
            blurBlob->Release();
            compositeBlob->Release();
            return SUCCEEDED(hr);
        }

        bool CreateTextureResource(UINT width, UINT height, D3D12_RESOURCE_FLAGS flags, ID3D12Resource** resource)
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = gFormat;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = flags;

            return SUCCEEDED(gDevice->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                nullptr,
                IID_PPV_ARGS(resource)));
        }

        bool CreateTextures()
        {
            ReleaseTextures();

            if (!CreateTextureResource(gWidth, gHeight, D3D12_RESOURCE_FLAG_NONE, &gSceneCopy))
                return false;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = gFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            gSceneCopySrvCpu = CpuSrvAt(kSrvHeapBaseIndex);
            gSceneCopySrvGpu = GpuSrvAt(kSrvHeapBaseIndex);
            gDevice->CreateShaderResourceView(gSceneCopy, &srvDesc, gSceneCopySrvCpu);

            for (UINT i = 0; i < kMaxLevels; ++i)
            {
                BlurLevel& level = gLevels[i];
                level.Width = (std::max)(gWidth >> (i + 1), 1u);
                level.Height = (std::max)(gHeight >> (i + 1), 1u);
                if (!CreateTextureResource(level.Width, level.Height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, &level.Resource))
                    return false;

                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format = gFormat;
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

                level.SrvCpu = CpuSrvAt(kSrvHeapBaseIndex + 1 + i);
                level.SrvGpu = GpuSrvAt(kSrvHeapBaseIndex + 1 + i);
                level.RtvCpu = RtvAt(i);

                gDevice->CreateShaderResourceView(level.Resource, &srvDesc, level.SrvCpu);
                gDevice->CreateRenderTargetView(level.Resource, &rtvDesc, level.RtvCpu);
            }

            return true;
        }

        void BarrierTransition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            if (before == after)
                return;

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
        }

        void SetFullscreenState(ID3D12GraphicsCommandList* cmd, UINT width, UINT height)
        {
            D3D12_VIEWPORT viewport{};
            viewport.TopLeftX = 0.0f;
            viewport.TopLeftY = 0.0f;
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            D3D12_RECT scissor{};
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<LONG>(width);
            scissor.bottom = static_cast<LONG>(height);

            cmd->RSSetViewports(1, &viewport);
            cmd->RSSetScissorRects(1, &scissor);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        void DrawBlurPass(
            ID3D12GraphicsCommandList* cmd,
            const D3D12_GPU_DESCRIPTOR_HANDLE& sourceSrv,
            ID3D12Resource* sourceResource,
            D3D12_RESOURCE_STATES sourceStateBefore,
            ID3D12Resource* targetResource,
            const D3D12_CPU_DESCRIPTOR_HANDLE& targetRtv,
            UINT targetWidth,
            UINT targetHeight,
            float radius)
        {
            BarrierTransition(cmd, sourceResource, sourceStateBefore, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            BarrierTransition(cmd, targetResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

            const float clear[4] = {};
            cmd->OMSetRenderTargets(1, &targetRtv, FALSE, nullptr);
            cmd->ClearRenderTargetView(targetRtv, clear, 0, nullptr);
            SetFullscreenState(cmd, targetWidth, targetHeight);
            cmd->SetPipelineState(gBlurPipeline);
            cmd->SetGraphicsRootDescriptorTable(0, sourceSrv);

            ShaderConstants constants{};
            constants.Values[0] = 1.0f / static_cast<float>((std::max)(targetWidth, 1u));
            constants.Values[1] = 1.0f / static_cast<float>((std::max)(targetHeight, 1u));
            constants.Values[2] = radius;
            cmd->SetGraphicsRoot32BitConstants(1, 24, constants.Values, 0);
            cmd->DrawInstanced(3, 1, 0, 0);
        }
    }

    bool Initialize(ID3D12Device* device, ID3D12DescriptorHeap* sharedSrvHeap, DXGI_FORMAT format, UINT width, UINT height)
    {
        if (!device || !sharedSrvHeap || width == 0 || height == 0)
            return false;

        gDevice = device;
        gSharedSrvHeap = sharedSrvHeap;
        gFormat = format;
        gWidth = width;
        gHeight = height;
        gSrvIncrement = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        gRtvIncrement = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        if (!gBlurRtvHeap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heapDesc.NumDescriptors = kMaxLevels;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gBlurRtvHeap))))
                return false;
        }

        if (!gRootSignature || !gBlurPipeline || !gCompositePipeline)
        {
            ReleasePipeline();
            if (!CreatePipelines())
                return false;
        }

        return CreateTextures();
    }

    void Shutdown()
    {
        ReleaseTextures();
        ReleasePipeline();
        if (gBlurRtvHeap) { gBlurRtvHeap->Release(); gBlurRtvHeap = nullptr; }
        gSharedSrvHeap = nullptr;
        gDevice = nullptr;
        gWidth = 0;
        gHeight = 0;
        gFormat = DXGI_FORMAT_UNKNOWN;
    }

    void Resize(UINT width, UINT height, DXGI_FORMAT format)
    {
        if (!gDevice || !gSharedSrvHeap || width == 0 || height == 0)
            return;

        if (width == gWidth && height == gHeight && format == gFormat && gSceneCopy)
            return;

        gWidth = width;
        gHeight = height;
        gFormat = format;
        ReleasePipeline();
        if (!CreatePipelines())
            return;
        CreateTextures();
    }

    void BeginGlowFrame()
    {
        gGlowRects.clear();
    }

    void SubmitGlowRect(const GlowRect& rect)
    {
        if (rect.Max.x <= rect.Min.x || rect.Max.y <= rect.Min.y)
            return;
        gGlowRects.push_back(rect);
    }

    void Render(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* backBuffer,
        const D3D12_CPU_DESCRIPTOR_HANDLE& backBufferRtv,
        const MenuBackdropState& backdropState)
    {
        if (!commandList || !backBuffer || !gSceneCopy || !backdropState.Visible || backdropState.Size.x <= 0.0f || backdropState.Size.y <= 0.0f)
            return;

        ID3D12DescriptorHeap* heaps[] = { gSharedSrvHeap };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootSignature(gRootSignature);

        BarrierTransition(commandList, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        BarrierTransition(commandList, gSceneCopy, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->CopyResource(gSceneCopy, backBuffer);
        BarrierTransition(commandList, gSceneCopy, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        BarrierTransition(commandList, backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        DrawBlurPass(commandList, gSceneCopySrvGpu, gSceneCopy, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, gLevels[0].Resource, gLevels[0].RtvCpu, gLevels[0].Width, gLevels[0].Height, 1.2f);
        for (UINT i = 1; i < kMaxLevels; ++i)
        {
            DrawBlurPass(commandList, gLevels[i - 1].SrvGpu, gLevels[i - 1].Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, gLevels[i].Resource, gLevels[i].RtvCpu, gLevels[i].Width, gLevels[i].Height, 1.1f + static_cast<float>(i) * 0.25f);
        }
        for (UINT i = kMaxLevels - 1; i > 0; --i)
        {
            DrawBlurPass(commandList, gLevels[i].SrvGpu, gLevels[i].Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, gLevels[i - 1].Resource, gLevels[i - 1].RtvCpu, gLevels[i - 1].Width, gLevels[i - 1].Height, 0.85f + static_cast<float>(i) * 0.15f);
        }

        BarrierTransition(commandList, gLevels[0].Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        commandList->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);
        SetFullscreenState(commandList, gWidth, gHeight);
        commandList->SetPipelineState(gCompositePipeline);
        commandList->SetGraphicsRootDescriptorTable(0, gLevels[0].SrvGpu);

        ShaderConstants constants{};
        constants.Values[0] = backdropState.Pos.x;
        constants.Values[1] = backdropState.Pos.y;
        constants.Values[2] = backdropState.Pos.x + backdropState.Size.x;
        constants.Values[3] = backdropState.Pos.y + backdropState.Size.y;
        constants.Values[4] = backdropState.Accent.x;
        constants.Values[5] = backdropState.Accent.y;
        constants.Values[6] = backdropState.Accent.z;
        constants.Values[7] = backdropState.Accent.w;
        constants.Values[8] = backdropState.Tint.x;
        constants.Values[9] = backdropState.Tint.y;
        constants.Values[10] = backdropState.Tint.z;
        constants.Values[11] = backdropState.Tint.w;
        constants.Values[12] = backdropState.Rounding;
        constants.Values[13] = backdropState.Opacity;
        constants.Values[14] = static_cast<float>(gWidth);
        constants.Values[15] = static_cast<float>(gHeight);
        constants.Values[16] = backdropState.Glow.x;
        constants.Values[17] = backdropState.Glow.y;
        constants.Values[18] = backdropState.Glow.z;
        constants.Values[19] = backdropState.Glow.w;
        constants.Values[20] = backdropState.GlowSpread;
        constants.Values[21] = 0.30f;
        constants.Values[22] = 18.0f;
        constants.Values[23] = backdropState.GlowStrength;
        commandList->SetGraphicsRoot32BitConstants(1, 24, constants.Values, 0);
        commandList->DrawInstanced(3, 1, 0, 0);

        for (const GlowRect& rect : gGlowRects)
        {
            ShaderConstants glowConstants{};
            glowConstants.Values[0] = rect.Min.x;
            glowConstants.Values[1] = rect.Min.y;
            glowConstants.Values[2] = rect.Max.x;
            glowConstants.Values[3] = rect.Max.y;
            glowConstants.Values[4] = 0.0f;
            glowConstants.Values[5] = 0.0f;
            glowConstants.Values[6] = 0.0f;
            glowConstants.Values[7] = 0.0f;
            glowConstants.Values[8] = backdropState.Pos.x;
            glowConstants.Values[9] = backdropState.Pos.y;
            glowConstants.Values[10] = backdropState.Pos.x + backdropState.Size.x;
            glowConstants.Values[11] = backdropState.Pos.y + backdropState.Size.y;
            glowConstants.Values[12] = rect.Rounding;
            glowConstants.Values[13] = 0.0f;
            glowConstants.Values[14] = static_cast<float>(gWidth);
            glowConstants.Values[15] = static_cast<float>(gHeight);
            glowConstants.Values[16] = rect.Color.x;
            glowConstants.Values[17] = rect.Color.y;
            glowConstants.Values[18] = rect.Color.z;
            glowConstants.Values[19] = rect.Color.w;
            glowConstants.Values[20] = backdropState.GlowSpread * rect.SpreadScale;
            glowConstants.Values[21] = 0.0f;
            glowConstants.Values[22] = 18.0f;
            glowConstants.Values[23] = backdropState.GlowStrength * rect.StrengthScale;
            commandList->SetGraphicsRoot32BitConstants(1, 24, glowConstants.Values, 0);
            commandList->DrawInstanced(3, 1, 0, 0);
        }
    }
}
