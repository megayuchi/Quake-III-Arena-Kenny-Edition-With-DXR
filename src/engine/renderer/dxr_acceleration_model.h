#pragma once

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include <vector>
#include <algorithm>

class dxr_acceleration_model
{
public:
	typedef enum { STATIC_MESH, DYNAMIC_MESH } meshType_t;

	dxr_acceleration_model();
	~dxr_acceleration_model();

	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT2 uv;
	};

	struct Mesh
	{
		UINT VertexCount;
		UINT IndexCount;

		meshType_t MeshType;

		bool Dirty;
		bool Update;

		UINT32 VertexBufferMegaPos;
		UINT32 IndexBufferMegaPos;
		int SurfaceIndex;
		int Width;
		int Height;

		std::vector<Vertex>		Vertices;
		std::vector<uint32_t>	Indices;
	};

	std::vector<Mesh>	meshes;
	int dirtyCount = 0;
	
	void Reset();

	UINT MeshCount();

	void AddMesh(meshType_t meshType, int surfaceIndex, int width, int height);

	int FindMesh(int surfaceIndex);

	void ResetMeshForUpdating(int i);

	meshType_t GetMeshType(int i);

	void AddVertexData(vec4_t xyz[SHADER_MAX_VERTEXES], vec2_t texCoords[SHADER_MAX_VERTEXES], vec4_t normals[SHADER_MAX_VERTEXES], int	numVertexes);	

	void AddVertexData(int meshIndex, float* points, vec4_t normals, int numVertexes, int vertexStride);

	void AddVertexData(int meshIndex, float* point, float* normal, float* uv);
	
	void AddIndexesData(int meshIndex, unsigned int indexes[SHADER_MAX_INDEXES], int numIndexes);

	void AddIndex(int meshIndex, unsigned int index);

	UINT MeshVertexCount(int i);

	const void* GetMeshVertexData(int i);

	UINT MeshIndexCount(int i);

	const void* GetMeshIndexData(int i);

	bool IsMeshDirty(int i);

	bool IsMeshUpdate(int i);

	void ClearDirty();

	int DirtyCount();

	void SetVertexBufferMegaPos(int i, UINT32 p);

	UINT32 GetVertexBufferMegaPos(int i);

	void SetIndexBufferMegaPos(int i, UINT32 p);

	UINT32 GetIndexBufferMegaPos(int i);

	void GetSurfaceIndex(int i, int& index, int& width, int& height);
};

