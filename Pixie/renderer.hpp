#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <SDL.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include "ext/d3dx12.h"
#include "utils.hpp"

using namespace DirectX;
namespace wrl = Microsoft::WRL;

namespace pxe {
	struct PixieVertexShader {
		XMFLOAT4X4 modelMatrix;
		XMFLOAT4X4 projectionMatrix;
		XMFLOAT4X4 viewMatrix;
	};
	
	struct PixieVertexData {
		XMFLOAT3 position;
		XMFLOAT2 uv;
	};

	// create a basic renderer
	class PixieRenderer {
	public:
		PixieRenderer(SDL_Window *window, UINT width, UINT height);
		~PixieRenderer();

		void loadPipeline();
		void createDevice();
		void createCMDQueue();
		void createSwapchain();
		void createFrameBuffer(); // descriptor heap
		void createCMDAllocator();
		void createRootSig();
		void createPipelineState();
		void createSyncStructure();
		void awaitFence();
		void handleCommands(FLOAT *color);
		void beginFrame(FLOAT *color);
		void endFrame();


	private:
		HWND hwnd;
		UINT surfaceWidth;
		UINT surfaceHeight;

		static const UINT bufferCount = 2;
		static const UINT texturePixelSize = 4; // 4 components = RGBA
		
		// pipeline
		wrl::ComPtr<IDXGIFactory7> factory;
		wrl::ComPtr<ID3D12Device> device;
		wrl::ComPtr<ID3D12CommandQueue> cmdQueue;
		wrl::ComPtr<ID3D12CommandAllocator> cmdAlloc;
		wrl::ComPtr<ID3D12RootSignature> rootSig;
		wrl::ComPtr<ID3D12PipelineState> pipelineState;
		wrl::ComPtr<ID3D12GraphicsCommandList> cmdList;
		CD3DX12_VIEWPORT viewport;
		CD3DX12_RECT scissor;
		UINT rtvDescSize;
		// frame buffer
		wrl::ComPtr<ID3D12DescriptorHeap> rtvHeap;
		wrl::ComPtr<ID3D12DescriptorHeap> srvHeap;
		wrl::ComPtr<ID3D12Resource> renderTargets[bufferCount];
		wrl::ComPtr<IDXGISwapChain3> swapchain;

		// resources
		wrl::ComPtr<ID3D12Resource> texture;
		wrl::ComPtr<ID3D12Resource> textureUploadHeap;
		wrl::ComPtr<ID3D12Resource> vertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
		wrl::ComPtr<ID3D12Resource> indexBuffer;
		wrl::ComPtr<ID3D12Resource> indexBufferUploadHeap;
		D3D12_INDEX_BUFFER_VIEW indexBufferView;

		// sync objects
		UINT frameIndex;
		UINT64 fenceVal;
		HANDLE fenceEvent;
		wrl::ComPtr<ID3D12Fence> fence;
	};
} // namespace pxe