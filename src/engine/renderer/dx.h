#pragma once

// Uncomment the following line to enable DX12 backend
#define ENABLE_DX12

#include <vector>
#include "dxr_acceleration_structure_manager.h"
#include "dxr_acceleration_model.h"
#include "dx_shaders.h"

struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList4;
struct ID3D12CommandQueue;
struct ID3D12Device5;
struct ID3D12DescriptorHeap;
struct ID3D12Fence;
struct ID3D12PipelineState;
struct ID3D12Resource;
struct ID3D12RootSignature;
struct IDXGISwapChain3;
struct ID3D12_ROOT_SIGNATURE_DESC;
struct D3D12_ROOT_SIGNATURE_DESC;

class dx_renderTargets;
class dx_postProcess;
class dx_postProcessTemporalReproject;
class dx_postProcessBlur;
class dx_postProcessComposite;
class dxr_lights;

//DRX
struct DXRGlobal;
struct D3D12Resources;
struct ViewCB;
struct MaterialCB;

enum Dx_Sampler_Index {
	SAMPLER_MIP_REPEAT,
	SAMPLER_MIP_CLAMP,
	SAMPLER_NOMIP_REPEAT,
	SAMPLER_NOMIP_CLAMP,
	SAMPLER_FULLSCREEN_CLAMP,
	SAMPLER_COUNT
};

struct Dx_Image {
	ID3D12Resource* texture = nullptr;
	Dx_Sampler_Index sampler_index = SAMPLER_COUNT;
};

enum Dx_Image_Format {
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_BGRA4,
	IMAGE_FORMAT_BGR5A1
};

struct D3D12BufferCreateInfo
{
	UINT64 size;
	UINT64 alignment;
	D3D12_HEAP_TYPE heapType;
	D3D12_RESOURCE_FLAGS flags;
	D3D12_RESOURCE_STATES state;

	D3D12BufferCreateInfo() :
		size(0), alignment(0),
		heapType(D3D12_HEAP_TYPE_DEFAULT),
		flags(D3D12_RESOURCE_FLAG_NONE),
		state(D3D12_RESOURCE_STATE_COMMON) {}

	D3D12BufferCreateInfo(UINT64 InSize, UINT64 InAlignment, D3D12_HEAP_TYPE InHeapType, D3D12_RESOURCE_FLAGS InFlags, D3D12_RESOURCE_STATES InState) :
		size(InSize), alignment(InAlignment),
		heapType(InHeapType),
		flags(InFlags),
		state(InState) {}

	D3D12BufferCreateInfo(UINT64 InSize, D3D12_RESOURCE_FLAGS InFlags, D3D12_RESOURCE_STATES InState) :
		size(InSize), alignment(0),
		heapType(D3D12_HEAP_TYPE_DEFAULT),
		flags(InFlags),
		state(InState) {}

	D3D12BufferCreateInfo(UINT64 InSize, D3D12_HEAP_TYPE InHeapType, D3D12_RESOURCE_STATES InState) :
		size(InSize), alignment(0),
		heapType(InHeapType),
		flags(D3D12_RESOURCE_FLAG_NONE),
		state(InState) {}

	D3D12BufferCreateInfo(UINT64 InSize, D3D12_RESOURCE_FLAGS InFlags) :
		size(InSize), alignment(0),
		heapType(D3D12_HEAP_TYPE_DEFAULT),
		flags(InFlags),
		state(D3D12_RESOURCE_STATE_COMMON) {}
};

//
// Initialization.
//
void dx_initialize();
ID3D12RootSignature* Create_Root_Signature(const D3D12_ROOT_SIGNATURE_DESC& desc);
void dx_shutdown();
void dx_release_resources();
void dx_wait_device_idle();

//
// Resources allocation.
//

Dx_Image dx_create_image(int width, int height, Dx_Image_Format format, int mip_levels, bool repeat_texture, int image_index);
void dx_upload_image_data(ID3D12Resource* texture, int width, int height, int mip_levels, const uint8_t* pixels, int bytes_per_pixel);
void dx_create_sampler_descriptor(const Vk_Sampler_Def& def, Dx_Sampler_Index sampler_index);

ID3D12PipelineState* dx_find_pipeline(const Vk_Pipeline_Def& def);

//
// Rendering setup.
//
void dx_clear_attachments(bool clear_depth_stencil, bool clear_color, vec4_t color);

void dxr_Reset();
int dxr_AddBottomLevelMesh(dxr_acceleration_model::meshType_t meshType, int surfaceIndex, int width, int height);
void dxr_AddBottomLevelMeshData(int bottomLevelIndex, unsigned *indices, float* points, int numIndices, int numPoints, vec3_t normal);

void dxr_AddBottomLeveIndexesData(int bottomLevelIndex, unsigned *indices, int numIndices);
void dxr_AddBottomLevelIndex(int bottomLevelIndex, unsigned index);
void dxr_AddBottomLevelVertex(int bottomLevelIndex, float	*xyz, float* normal, float* uv);

void dxr_AddTopLevelIndexWithTransform(int bottomLevelIndex, vec3_t axis[3], float origin[3]);
void dxr_ResetMeshForUpdating(int bottomLevelIndex);

void dxr_AddWorldSurfaceGeometry(unsigned *indices, float* points, int numIndices, int numPoints, vec3_t normal, int surfaceIndex, int width, int height);
int dxr_AddWorldSurface(int surfaceIndex, int width, int height);

UINT dxr_MeshVertexCount(int bottomLevelIndex);

void dx_bind_geometry();
void dx_shade_geometry(ID3D12PipelineState* pipeline, bool multitexture, Vk_Depth_Range depth_range, bool indexed, bool lines);
void SetupTagets();
void SetupTagetsDXR();
void dx_begin_frame();
void dx_begin_frame_DXR();
void dx_end_frame();
void dx_end_frame_DXR();
void Start3d();
void End3d();
void GetLastTransformFromCache(const void *ent, float newTransform[16], float out_lastTransform[16], int frameCount);
void AddLevelLight(const vec3_t origin, const vec3_t color, float size);

void Create_Buffer(D3D12BufferCreateInfo& info, ID3D12Resource** ppResource);
void Create_Constant_Buffer(ID3D12Resource** buffer, UINT64 size);

struct ShaderConstanceViewCB
{
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX viewLast;
	DirectX::XMMATRIX projMatrix;

	ShaderConstanceViewCB()
	{
		view = DirectX::XMMatrixIdentity();
		viewLast = DirectX::XMMatrixIdentity();
		projMatrix = DirectX::XMMatrixIdentity();
	}
};

struct Dx_Instance
{
	bool active = false;

	ID3D12Device5* device = nullptr;
	ID3D12CommandQueue* command_queue = nullptr;
	IDXGISwapChain3* swapchain = nullptr;
	UINT frame_index = 0;

	ID3D12CommandAllocator* command_allocator = nullptr;
	ID3D12CommandAllocator* helper_command_allocator = nullptr;
	ID3D12GraphicsCommandList4* command_list = nullptr;
	
	ID3D12Fence* fence = nullptr;
	UINT64 fence_value = 0;
	HANDLE fence_event = NULL;

	ID3D12RootSignature* root_signature = nullptr;	

	//
	// Geometry buffers.
	//
	byte* vertex_buffer_ptr = nullptr; // pointer to mapped vertex buffer
	int xyz_elements = 0;
	int color_st_elements = 0;

	byte* index_buffer_ptr = nullptr; // pointer to mapped index buffer
	int index_buffer_offset = 0;

	ID3D12Resource* geometry_buffer = nullptr;

	//
	// Standard pipelines.
	//
	ID3D12PipelineState* skybox_pipeline = nullptr;

	// dim 0: 0 - front side, 1 - back size
	// dim 1: 0 - normal view, 1 - mirror view
	ID3D12PipelineState* shadow_volume_pipelines[2][2];
	ID3D12PipelineState* shadow_finish_pipeline = nullptr;

	// dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	ID3D12PipelineState* fog_pipelines[2][3][2];

	// dim 0 is based on dlight additive flag: 0 - not additive, 1 - additive
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	ID3D12PipelineState* dlight_pipelines[2][3][2];

	// debug visualization pipelines
	ID3D12PipelineState* tris_debug_pipeline = nullptr;
	ID3D12PipelineState* tris_mirror_debug_pipeline = nullptr;
	ID3D12PipelineState* normals_debug_pipeline = nullptr;
	ID3D12PipelineState* surface_debug_pipeline_solid = nullptr;
	ID3D12PipelineState* surface_debug_pipeline_outline = nullptr;
	ID3D12PipelineState* images_debug_pipeline = nullptr;

	dx_renderTargets* dx_renderTargets = nullptr;
	dx_postProcessTemporalReproject* dx_postProcessTemporalReproject = nullptr;
	dx_postProcessBlur* dx_postProcessBlur = nullptr;
	dx_postProcessComposite* dx_postProcessComposite = nullptr;
	dxr_lights* dxr_lights = nullptr;

	ID3D12Resource* shaderConstantView = nullptr;
	ShaderConstanceViewCB shaderConstanceViewCB;
	UINT8* shaderConstanceViewCBStart;

	//DXR
	bool dxr_initialized{ false };
	D3DShaders::D3D12ShaderCompilerInfo* shaderCompiler;
	
	int width;
	int height;

	DXRGlobal* dxr = {};
	D3D12Resources* resources = {};

	bool inSide3d = false;
};

struct Dx_World {
	//
	// Resources.
	//
	int num_pipelines = 0;
	Vk_Pipeline_Def pipeline_defs[MAX_VK_PIPELINES];
	ID3D12PipelineState* pipelines[MAX_VK_PIPELINES];
	float pipeline_create_time;

	Dx_Image images[MAX_VK_IMAGES];

	//
	// State.
	//
	int current_image_indices[2];
	float modelview_transform[16];
	float model_transform[16];
	float model_transformLast[16];

	float view_transform[16];
	float view_transformLast[16];
	float view_transform3D[16];
	float view_transformLast3D[16];
	float proj_transform[16];

	float zNear;
	float zFar;

	vec3_t		viewOrg;
	vec3_t		viewaxisForword;
	vec2_t		viewFov;
	int			rdflags;			// RDF_NOWORLDMODEL, etc

	//DXR
	ID3D12Resource*									viewCB;
	ViewCB*											viewCBData;
	UINT8*											viewCBStart;
};
