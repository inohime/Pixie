#include "renderer.hpp"
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <SDL_image.h>
#include <SDL_syswm.h>
#include <iostream>
#include <format>
#include <vector>
#include <sstream>

namespace pxe {
    PixieRenderer::PixieRenderer(SDL_Window *window, UINT width, UINT height)
        : surfaceWidth(width)
        , surfaceHeight(height)
        , factory(nullptr)
        , device(nullptr)
        , cmdQueue(nullptr)
        , cmdAlloc(nullptr)
        , rootSig(nullptr)
        , pipelineState(nullptr)
        , cmdList(nullptr)
        , viewport(0.0f, 0.0f, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight))
        , scissor(0, 0, static_cast<LONG>(surfaceWidth), static_cast<LONG>(surfaceHeight))
        , rtvHeap(nullptr)
        , srvHeap(nullptr)
        , swapchain(nullptr)
        , vertexBuffer(nullptr)
        , fence(nullptr) {

        for (size_t i = 0; i < bufferCount; ++i)
            renderTargets[i] = nullptr;

        SDL_SysWMinfo WMinfo;
        SDL_VERSION(&WMinfo.version);
        SDL_GetWindowWMInfo(window, &WMinfo);
        hwnd = (HWND)WMinfo.info.win.window;

        loadPipeline();
    }

    PixieRenderer::~PixieRenderer() {
        awaitFence();

        CloseHandle(fenceEvent);
    }

    // Check for gpus and choose the best one if it supports high performance mode
    static void searchForPerformanceAdapter(IDXGIFactory1 *pFactory, IDXGIAdapter1 **ppAdapter, bool software = false) {
        std::cout << "Checking adapter..\n";

        *ppAdapter = nullptr;

        wrl::ComPtr<IDXGIAdapter1> adapter;
        wrl::ComPtr<IDXGIFactory6> factory;

        if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory)))) {
            for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); ++i) {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                // Check whether the device supports d3d12 or not
                const auto result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
                if (SUCCEEDED(result)) {
                    std::cout << "Found a suitable adapter!\n";
                    std::wcout << std::format(L"Using adapter: {}", desc.Description) << '\n';
                    break;
                }
            }
        } else if (adapter.Get() == nullptr) {
            for (UINT i = 0; SUCCEEDED(pFactory->EnumAdapters1(i, &adapter)); ++i) {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                //Check whether the device supports d3d12 or not
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
                    std::cout << "Found a suitable adapter!\n";
                    std::wcout << std::format(L"Using adapter: {}", desc.Description) << '\n';
                    break;
                }
            }
        }

        *ppAdapter = adapter.Detach();
    }

    void PixieRenderer::loadPipeline() {
        createDevice();
        std::cout << "created device\n";
        createCMDQueue();
        std::cout << "created command queue\n";
        createSwapchain();
        std::cout << "created swapchain\n";
        createFrameBuffer();
        std::cout << "created frame buffer\n";
        createCMDAllocator();
        std::cout << "created command allocator\n";
        createRootSig();
        std::cout << "created root signature\n";
        createPipelineState();
        std::cout << "created pipeline state\n";
        createSyncStructure();
        std::cout << "created sync structures\n";
    }

    void PixieRenderer::createDevice() {
        UINT DXGIFlags = 0;

#ifdef _DEBUG
        // Enable debug layer
        {
            wrl::ComPtr<ID3D12Debug> debug;
            throwIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
            debug->EnableDebugLayer();
            DXGIFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif

        throwIfFailed(CreateDXGIFactory2(DXGIFlags, IID_PPV_ARGS(&factory)));

        wrl::ComPtr<IDXGIAdapter1> adapter;
        searchForPerformanceAdapter(factory.Get(), &adapter, true);

        throwIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    }

    void PixieRenderer::createCMDQueue() {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        throwIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue)));
    }

    void PixieRenderer::createSwapchain() {
        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.BufferCount = bufferCount;
        swapchainDesc.Width = surfaceWidth;
        swapchainDesc.Height = surfaceHeight;
        swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.SampleDesc.Quality = 0;
        swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        wrl::ComPtr<IDXGISwapChain1> tmpSwapchain;
        throwIfFailed(factory->CreateSwapChainForHwnd(cmdQueue.Get(), hwnd, &swapchainDesc, nullptr, nullptr, &tmpSwapchain));

        // enable fullscreen transitions later
        throwIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

        throwIfFailed(tmpSwapchain.As(&swapchain));
    }

    void PixieRenderer::createFrameBuffer() {
        frameIndex = swapchain->GetCurrentBackBufferIndex();

        // create descriptor heaps
        {
            // render target view descriptor heap
            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
            rtvHeapDesc.NumDescriptors = bufferCount;
            rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            throwIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));

            // shader resource view descriptor heap
            D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
            srvHeapDesc.NumDescriptors = 1;
            srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            throwIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)));

            rtvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }

        // frame buffer
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

            for (UINT i = 0; i < bufferCount; ++i) {
                throwIfFailed(swapchain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])));
                device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHandle);
                rtvHandle.Offset(1, rtvDescSize);
            }
        }
    }

    void PixieRenderer::createCMDAllocator() {
        throwIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc)));
    }

    void PixieRenderer::createRootSig() {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1] = {}; // remove braces later
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParameters[1] = {};
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        wrl::ComPtr<ID3DBlob> signature;
        wrl::ComPtr<ID3DBlob> error;

        throwIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        throwIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
    }

    void PixieRenderer::createPipelineState() {
        // optionally break this into @ createShader(path, entryPoint, version)
        {
            wrl::ComPtr<ID3DBlob> vertexShader;
            wrl::ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
            // Enable better shader debugging with the graphics debugging tools.
            UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            UINT compileFlags = 0;
#endif

            throwIfFailed(D3DCompileFromFile((L"Pixie/assets/shaders.hlsl"), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
            throwIfFailed(D3DCompileFromFile(L"Pixie/assets/shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

            D3D12_INPUT_ELEMENT_DESC inputElemDesc[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElemDesc, _countof(inputElemDesc)};
            psoDesc.pRootSignature = rootSig.Get();
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;

            throwIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
            pipelineState->SetName(L"Pipeline State Object");
        }

        // Create the command list.
        throwIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), pipelineState.Get(), IID_PPV_ARGS(&cmdList)));
        cmdList->SetName(L"Command List");

        // Create the vertex buffer.
        {
            /*
				{{0.0f, 0.25f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {0.5f, 0.0f}},
				{{0.25f, -0.25f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {1.0f, 1.0f}},
				{{-0.25f, -0.25f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {0.0f, 1.0f}},
			*/
            PixieVertexData triangleVertices[] = {
                {{-0.5f, 0.5f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {0.5f, 0.0f}}, // top left
                {{0.5f, -0.5f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {1.0f, 1.0f}}, // bottom right
                {{-0.5f, -0.5f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {0.0f, 1.0f}}, // bottom left
                {{0.5f, 0.5f * static_cast<FLOAT>(surfaceWidth / surfaceHeight), 0.0f}, {0.5f, 0.0f}} // top right
            };

            //const UINT vertexBufferSize = sizeof(triangleVertices);
            /*
			auto uploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangleVertices));

			throwIfFailed(device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer)));
			vertexBuffer->SetName(L"Vertex Buffer Resource Heap");

			// Copy the triangle data to the vertex buffer.
			UINT8 *vertexDataBegin = nullptr; // remove 
			CD3DX12_RANGE readRange(0, 0);
			throwIfFailed(vertexBuffer->Map(0, &readRange, reinterpret_cast<LPVOID *>(&vertexDataBegin)));
			memcpy(vertexDataBegin, triangleVertices, sizeof(triangleVertices));
			vertexBuffer->Unmap(0, nullptr);

			// Initialize the vertex buffer view.
			vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
			vertexBufferView.StrideInBytes = sizeof(PixieVertexData);
			vertexBufferView.SizeInBytes = sizeof(triangleVertices) * 4;
		//}

		//{
			DWORD quadIndices[] = {
				0, 1, 2, // first triangle
				0, 3, 1 // second triangle
			};

			auto indexProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			auto indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(quadIndices));

			throwIfFailed(device->CreateCommittedResource(&indexProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&indexBuffer)));
			indexBuffer->SetName(L"Index Buffer Resource Heap");

			auto indexUploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			//auto vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangleVertices));

			throwIfFailed(device->CreateCommittedResource(&indexUploadProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBufferUploadHeap)));
			indexBufferUploadHeap->SetName(L"Index Buffer Upload Resource Heap");

			D3D12_SUBRESOURCE_DATA indexData = {};
			indexData.pData = reinterpret_cast<BYTE *>(quadIndices);
			indexData.RowPitch = sizeof(quadIndices);
			indexData.SlicePitch = sizeof(quadIndices);

			UpdateSubresources(cmdList.Get(), indexBuffer.Get(), indexBufferUploadHeap.Get(), 0, 0, 1, &indexData);

			auto resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			cmdList->ResourceBarrier(1, &resBarrier);

			indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
			indexBufferView.Format = DXGI_FORMAT_R32_UINT;
			indexBufferView.SizeInBytes = sizeof(quadIndices);
			*/
        }

        // Create the texture.
        {
            // Load the test image
            SDL_Surface *surf = IMG_Load("Pixie/assets/icon.png");
            int textureWidth = surf->w;
            int textureHeight = surf->h;

            D3D12_RESOURCE_DESC textureDesc = {};
            textureDesc.MipLevels = 1;
            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDesc.Width = textureWidth;
            textureDesc.Height = textureHeight;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            textureDesc.DepthOrArraySize = 1;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

            auto textureProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

            throwIfFailed(device->CreateCommittedResource(&textureProps, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));
            texture->SetName(L"Texture Resource Heap");

            const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

            // Create the GPU upload buffer.
            auto uploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

            throwIfFailed(device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUploadHeap)));
            textureUploadHeap->SetName(L"Texture Upload Resource Heap");

            // copy the pitch and yaw to upload heap and push that data into Texture2D
            std::vector<UINT8> textureBytes;
            textureBytes.resize(static_cast<size_t>(textureWidth) * static_cast<size_t>(textureHeight) * texturePixelSize);
            std::memcpy(textureBytes.data(), surf->pixels, textureBytes.size());

            SDL_FreeSurface(surf);

            D3D12_SUBRESOURCE_DATA textureData = {};
            textureData.pData = &textureBytes[0];
            textureData.RowPitch = textureWidth * texturePixelSize;
            textureData.SlicePitch = textureData.RowPitch * textureHeight;

            UpdateSubresources(cmdList.Get(), texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
            auto resBarrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmdList->ResourceBarrier(1, &resBarrier);

            // Describe and create a SRV for the texture.
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = textureDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHeap->GetCPUDescriptorHandleForHeapStart());
        }

        // Close the command list and execute it to set gpu initial state
        throwIfFailed(cmdList->Close());

        ID3D12CommandList *ppCommandLists[] = {cmdList.Get()};
        cmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    }

    void PixieRenderer::createSyncStructure() {
        throwIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        fenceVal = 1;

        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (fenceEvent == nullptr)
            throwIfFailed(HRESULT_FROM_WIN32(GetLastError()));

        awaitFence();
    }

    void PixieRenderer::awaitFence() {
        const UINT64 fence = fenceVal;

        throwIfFailed(cmdQueue->Signal(this->fence.Get(), fence));

        fenceVal++;

        // Wait until the prev frame is finished
        if (this->fence->GetCompletedValue() < fence) {
            throwIfFailed(this->fence->SetEventOnCompletion(fence, fenceEvent));

            WaitForSingleObject(fenceEvent, INFINITE);
        }

        frameIndex = swapchain->GetCurrentBackBufferIndex();
    }

    void PixieRenderer::handleCommands(FLOAT *color) {
        throwIfFailed(cmdAlloc->Reset());

        throwIfFailed(cmdList->Reset(cmdAlloc.Get(), pipelineState.Get()));

        cmdList->SetGraphicsRootSignature(rootSig.Get());

        ID3D12DescriptorHeap *ppHeaps[] = {srvHeap.Get()};
        cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        cmdList->SetGraphicsRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);

        // Set back buffer as render target
        auto targetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->ResourceBarrier(1, &targetBarrier);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescSize);
        cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // Record commands
        cmdList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
        //cmdList->DrawInstanced(3, 1, 0, 0);
        cmdList->IASetIndexBuffer(&indexBufferView);
        cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);

        // Present back buffer
        auto presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        cmdList->ResourceBarrier(1, &presentBarrier);

        throwIfFailed(cmdList->Close());
    }

    void PixieRenderer::beginFrame(FLOAT *color) {
        handleCommands(color);
    }

    void PixieRenderer::endFrame() {
        ID3D12CommandList *cmdBuffer[] = {cmdList.Get()};
        (*cmdBuffer)->SetName(L"command list buffer");

        cmdQueue->ExecuteCommandLists(_countof(cmdBuffer), cmdBuffer);

        swapchain->Present(1, 0);
        awaitFence();
    }
} // namespace pxe
