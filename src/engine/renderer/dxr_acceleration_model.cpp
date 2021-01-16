#include "tr_local.h"

#include "dxr_acceleration_model.h"

dxr_acceleration_model::dxr_acceleration_model()
{
}


dxr_acceleration_model::~dxr_acceleration_model()
{
}


void dxr_acceleration_model::Reset()
{
	for (auto& m : meshes)
	{
		m.Vertices.clear();
		m.Indices.clear();
	}

	meshes.clear();
	int dirtyCount = 0;
}

UINT dxr_acceleration_model::MeshCount()
{
	return (UINT)meshes.size();
}

void dxr_acceleration_model::AddMesh(meshType_t meshType, int surfaceIndex, int width, int height)
{
	Mesh m;
	m.VertexCount = 0;
	m.IndexCount = 0;
	m.MeshType = meshType;
	m.Dirty = true;
	m.Update = false;
	m.VertexBufferMegaPos = -1;
	m.IndexBufferMegaPos = -1;
	m.SurfaceIndex = surfaceIndex;
	m.Width = width;
	m.Height = height;

	meshes.push_back(m);
	dirtyCount++;
}

int dxr_acceleration_model::FindMesh(int surfaceIndex)
{
	//TODO speed up with HashMap
	int count = (int)meshes.size();
	for (int i = 0; i < count; ++i)
	{
		const auto& m = meshes[i];
		if (surfaceIndex == m.SurfaceIndex)
		{
			return i;
		}
	}
	return -1;
}

void dxr_acceleration_model::ResetMeshForUpdating(int i)
{
	auto& m = meshes[i];
	m.VertexCount = 0;
	m.Vertices.clear();

	if (-1 != m.VertexBufferMegaPos || -1 != m.IndexBufferMegaPos)//check is the model is new, it may not have been rendered yet
	{
		m.Update = true;
	}

	dirtyCount++;
}

dxr_acceleration_model::meshType_t dxr_acceleration_model::GetMeshType(int i)
{
	auto& m = meshes[i];
	return m.MeshType;
}

void dxr_acceleration_model::AddVertexData(vec4_t xyz[SHADER_MAX_VERTEXES], vec2_t texCoords[SHADER_MAX_VERTEXES], vec4_t normals[SHADER_MAX_VERTEXES], int	numVertexes)
{
	auto& m = meshes.back();

	for (int i = 0; i < numVertexes; ++i)
	{
		Vertex v;
		v.position = DirectX::XMFLOAT3(xyz[i][0], xyz[i][1], xyz[i][2]);
		v.normal = DirectX::XMFLOAT3(normals[i][0], normals[i][1], normals[i][2]);
		m.Vertices.push_back(v);
	}

	m.VertexCount += numVertexes;
}

float frac(float v)
{
	return v - floor(v);
}

void dxr_acceleration_model::AddVertexData(int meshIndex, float* points, vec4_t normals, int numVertexes, int vertexStride)
{
	auto& m = meshes[meshIndex];
	for (int i = 0; i < numVertexes; ++i)
	{
		Vertex v;
		v.position = DirectX::XMFLOAT3(points[0], points[1], points[2]);
		v.normal = DirectX::XMFLOAT3(normals[0], normals[1], normals[2]);

		v.uv = DirectX::XMFLOAT2(points[3], points[4]);
		m.Vertices.push_back(v);

		points += vertexStride;
	}

	m.VertexCount += numVertexes;
}

void dxr_acceleration_model::AddVertexData(int meshIndex, float* point, float* normal, float* uv)
{
	auto& m = meshes[meshIndex];

	Vertex v;
	v.position = DirectX::XMFLOAT3(point[0], point[1], point[2]);
	v.normal = DirectX::XMFLOAT3(normal[0], normal[1], normal[2]);
	v.uv = DirectX::XMFLOAT2(uv[0], uv[1]);
	m.Vertices.push_back(v);

	m.VertexCount += 1;
}

void dxr_acceleration_model::AddIndexesData(int meshIndex, unsigned int indexes[SHADER_MAX_INDEXES], int numIndexes)
{
	auto& m = meshes[meshIndex];

	UINT vertexCount = m.VertexCount;
	for (int i = 0; i < numIndexes; i++)
	{
		UINT index = indexes[i] + vertexCount;
		m.Indices.push_back(index);
	}

	m.IndexCount += numIndexes;
}

void dxr_acceleration_model::AddIndex(int meshIndex, unsigned int index)
{
	auto& m = meshes[meshIndex];

	UINT vertexCount = m.VertexCount;
	m.Indices.push_back(index + vertexCount);

	m.IndexCount += 1;
}

UINT dxr_acceleration_model::MeshVertexCount(int i)
{
	auto& m = meshes[i];
	return m.VertexCount;
}

const void* dxr_acceleration_model::GetMeshVertexData(int i)
{
	auto& m = meshes[i];
	return &m.Vertices.data()[0];
}

UINT dxr_acceleration_model::MeshIndexCount(int i)
{
	auto& m = meshes[i];
	return m.IndexCount;
}

const void* dxr_acceleration_model::GetMeshIndexData(int i)
{
	auto& m = meshes[i];
	return &m.Indices.data()[0];
}

bool dxr_acceleration_model::IsMeshDirty(int i)
{
	auto& m = meshes[i];
	return m.Dirty;
}

bool dxr_acceleration_model::IsMeshUpdate(int i)
{
	auto& m = meshes[i];
	return m.Update;
}

void dxr_acceleration_model::ClearDirty()
{
	for (Mesh& m : meshes)
	{
		m.Dirty = false;
		m.Update = false;
	}
	dirtyCount = 0;
}

int dxr_acceleration_model::DirtyCount()
{
	return dirtyCount;
}

void dxr_acceleration_model::SetVertexBufferMegaPos(int i, UINT32 p)
{
	auto& m = meshes[i];
	m.VertexBufferMegaPos = p;
}

UINT32 dxr_acceleration_model::GetVertexBufferMegaPos(int i)
{
	if (i < meshes.size())
	{
		auto& m = meshes[i];
		return m.VertexBufferMegaPos;
	}
	else
	{
		return 0;
	}
}

void dxr_acceleration_model::SetIndexBufferMegaPos(int i, UINT32 p)
{
	auto& m = meshes[i];
	m.IndexBufferMegaPos = p;
}

UINT32 dxr_acceleration_model::GetIndexBufferMegaPos(int i)
{
	if (i < meshes.size())
	{
		auto& m = meshes[i];
		return m.IndexBufferMegaPos;
	}
	else
	{
		return 0;
	}
}

void dxr_acceleration_model::GetSurfaceIndex(int i, int& index, int& width, int& height)
{
	if (i < meshes.size())
	{
		auto& m = meshes[i];
		index = m.SurfaceIndex;
		width = m.Width;
		height = m.Height;
	}
	else
	{
		index = 0;
		width = 16;
		height = 16;
	}
}