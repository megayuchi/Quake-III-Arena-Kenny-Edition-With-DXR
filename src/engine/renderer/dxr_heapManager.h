#pragma once


#include <map>

struct Dx_Instance;
struct DXRGlobal;
struct Dx_World;

class dxr_heapManager
{
private:
	struct TextureNode
	{
		int ImageIndex;
		ID3D12Resource* Texture;
		DXGI_FORMAT DxFormat;
		int MipLevels;
	};
public:
	dxr_heapManager();
	~dxr_heapManager();

	void Reset();

	static dxr_heapManager* Get() { return s_singletonPtr; }

	void Create_CBVSRVUAV_Heap(Dx_Instance &d3d, DXRGlobal &dxr, Dx_World &world);

	void AddGameTextureViews(int image_index, ID3D12Resource* texture, DXGI_FORMAT dx_format, int mip_levels);
	void AddGameTextureViews();
	D3D12_GPU_DESCRIPTOR_HANDLE GetGameSrvHandle(int textureIndex);

	ID3D12DescriptorHeap* GetHeap() { return mCbvSrvUavRayGenHeaps; }

private:

	static dxr_heapManager* s_singletonPtr;

	void ConstantBufferView(D3D12_CPU_DESCRIPTOR_HANDLE handle, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation, UINT sizeInBytes);
	void UnorderedAccessViewTexture2D(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* renderTargetTexture);
	void RaytracingAccelerationStructureView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* accelerationStructure);
	void BufferShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* buffer, UINT numElements);
	void Texture2DShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* texture, DXGI_FORMAT format);
	void Texture2DArrayShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE handle, ID3D12Resource* texture, UINT arraySize);

	UINT mHandleIncrement{0};
	ID3D12DescriptorHeap* mCbvSrvUavRayGenHeaps;
	ID3D12Device5* mDevice = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE mGameTextureStartHandle;

	std::map<int, TextureNode> mTextureNodeList;

	bool mGameTexturesLoaded{false};
};

