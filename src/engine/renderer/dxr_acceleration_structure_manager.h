#pragma once

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include <vector>
#include <algorithm>


class dxr_acceleration_structure_manager
{
public:

	struct InstancesTransform
	{
		float data[3][4];
	};

	dxr_acceleration_structure_manager();
	~dxr_acceleration_structure_manager();

	void Reset();

	int EndFrame();
	
	int MeshCount()
	{
		return mModel.MeshCount();
	}

	size_t VertexSize()
	{
		return sizeof(Model::Vertex);
	}

	int AddMesh()
	{
		int id = mModel.MeshCount();
		mModel.AddMesh();

		return id;
	}

	void AddInstance(int i);
	void AddInstanceWithTransform(int i, vec3_t axis[3], float origin[3]);

	int GetInstanceCount()
	{
		return (int)mTopLevelInstances.size();
	}

	int* GetInstanceData()
	{
		return mTopLevelInstances.data();
	}

	std::vector<InstancesTransform>& GetInstanceTransformData()
	{
		return mTopLevelInstanceTransforms;
	}

	UINT MeshVertexCount(int i)
	{
		return mModel.MeshVertexCount(i);
	}

	UINT MeshIndexCount(int i)
	{
		return mModel.MeshIndexCount(i);
	}

	UINT MeshStartVertexIndex(int i)
	{
		return mModel.MeshStartVertexIndex(i);
	}
	
	UINT MeshStartIndexIndex(int i)
	{
		return mModel.MeshStartIndexIndex(i);
	}

	const void* GetMeshVertexData(int i)
	{
		return mModel.GetMeshVertexData(i);
	}

	const void* GetMeshIndexData(int i)
	{
		return mModel.GetMeshIndexData(i);
	}

	bool IsMeshDirty(int i)
	{
		return mModel.IsMeshDirty(i);
	}

	void AddVertexData(vec4_t xyz[SHADER_MAX_VERTEXES], vec2_t texCoords[SHADER_MAX_VERTEXES], vec4_t normals[SHADER_MAX_VERTEXES], int	numVertexes)
	{
		mModel.AddVertexData(xyz, texCoords, normals, numVertexes);
	}

	void AddVertexData(float* points, vec4_t normal, int	numVertexes)
	{
		mModel.AddVertexData(points, normal, numVertexes);
	}

	void AddVertexData(float* point, float* normal)
	{
		mModel.AddVertexData(point, normal);
	}

	void AddIndexesData(glIndex_t indexes[SHADER_MAX_INDEXES], int numIndexes)
	{
		mModel.AddIndexesData(indexes, numIndexes);
	}

	void AddIndex(glIndex_t index)
	{
		mModel.AddIndex(index);
	}
	
	void ClearDirty()
	{
		mModel.ClearDirty();
	}

	int DirtyCount()
	{
		return mModel.DirtyCount();
	}

	void WriteMeshDebug(int i, InstancesTransform& transform);
	void WriteAllTopLevelMeshsDebug();
	void WriteAllBottomLevelMeshsDebug();
	
	struct Model
	{
		struct Vertex
		{
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT3 normal;
		};

		struct Mesh
		{
			UINT StartVertexIndex;
			UINT StartIndexIndex;

			UINT VertexCount;
			UINT IndexCount;

			bool Dirty;
		};

		std::vector<Vertex>		vertices;
		std::vector<uint32_t>	indices;

		std::vector<Mesh>	meshes;
		int dirtyCount = 0;

		void Reset()
		{
			vertices.clear();
			indices.clear();
			meshes.clear();
			int dirtyCount = 0;
		}

		UINT MeshCount()
		{
			return (UINT)meshes.size();
		}

		void AddMesh()
		{
			Mesh m;
			m.StartVertexIndex = (UINT)vertices.size();
			m.StartIndexIndex = (UINT)indices.size();
			m.VertexCount = 0;
			m.IndexCount = 0;
			m.Dirty = true;
			meshes.push_back(m);
			dirtyCount++;
		}

		void AddVertexData(vec4_t xyz[SHADER_MAX_VERTEXES], vec2_t texCoords[SHADER_MAX_VERTEXES], vec4_t normals[SHADER_MAX_VERTEXES], int	numVertexes)
		{
			for (int i = 0; i < numVertexes; ++i)
			{
				Vertex v;
				v.position = DirectX::XMFLOAT3(xyz[i][0], xyz[i][1], xyz[i][2]);
				v.normal = DirectX::XMFLOAT3(normals[i][0], normals[i][1], normals[i][2]);
				vertices.push_back(v);
			}
			Model::Mesh& m = meshes.back();
			m.VertexCount += numVertexes;
		}

		void AddVertexData(float* points, vec4_t normals, int	numVertexes)
		{
			for (int i = 0; i < numVertexes; ++i)
			{
				Vertex v;
				v.position = DirectX::XMFLOAT3(points[0], points[1], points[2]);
				points += VERTEXSIZE;
				v.normal = DirectX::XMFLOAT3(normals[0], normals[1], normals[2]);
				vertices.push_back(v);
			}
			Model::Mesh& m = meshes.back();
			m.VertexCount += numVertexes;
		}

		void AddVertexData(float* point, float* normal)
		{
			Vertex v;
			v.position = DirectX::XMFLOAT3(point[0], point[1], point[2]);
			v.normal = DirectX::XMFLOAT3(normal[0], normal[1], normal[2]);
			vertices.push_back(v);

			Model::Mesh& m = meshes.back();
			m.VertexCount += 1;
		}

		void AddIndexesData(glIndex_t indexes[SHADER_MAX_INDEXES], int numIndexes)
		{
			Model::Mesh& m = meshes.back();

			UINT vertexCount = m.VertexCount;
			for (int i = 0; i < numIndexes; i++)
			{
				UINT index = indexes[i] + vertexCount;
				indices.push_back(index);
			}

			m.IndexCount += numIndexes;
		}

		void AddIndex(glIndex_t index)
		{
			Model::Mesh& m = meshes.back();

			UINT vertexCount = m.VertexCount;
			indices.push_back(index + vertexCount);

			m.IndexCount += 1;
		}

		UINT MeshVertexCount(int i)
		{
			Model::Mesh& m = meshes[i];
			return m.VertexCount;
		}

		const void* GetMeshVertexData(int i)
		{
			Model::Mesh& m = meshes[i];
			return &vertices.data()[m.StartVertexIndex];
		}

		UINT MeshIndexCount(int i)
		{
			Model::Mesh& m = meshes[i];
			return m.IndexCount;
		}

		UINT MeshStartVertexIndex(int i)
		{
			if (i < meshes.size())
			{
				Model::Mesh& m = meshes[i];
				return m.StartVertexIndex;
			}
			else
			{
				return 0;
			}
		}

		UINT MeshStartIndexIndex(int i)
		{
			if (i < meshes.size())
			{
				Model::Mesh& m = meshes[i];
				return m.StartIndexIndex;
			}
			else
			{
				return 0;
			}
		}

		const void* GetMeshIndexData(int i)
		{
			Model::Mesh& m = meshes[i];
			return &indices.data()[m.StartIndexIndex];
		}
		
		bool IsMeshDirty(int i)
		{
			Model::Mesh& m = meshes[i];
			return m.Dirty;
		}

		void ClearDirty()
		{
			for (Mesh& m : meshes)
			{
				m.Dirty = false;
			}
			dirtyCount = 0;
		}

		int DirtyCount()
		{
			return dirtyCount;
		}
	};

private:

	Model::Vertex MatrixMul(InstancesTransform& transform, Model::Vertex vecIn);

	bool mBuildOnce;

	Model mModel;
	std::vector<int> mTopLevelInstances;

	std::vector<InstancesTransform> mTopLevelInstanceTransforms;

	int mVerticesWritenDebug;
};

