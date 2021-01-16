#pragma once

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include <vector>
#include <algorithm>

#include "dxr_acceleration_model.h"


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
		return sizeof(dxr_acceleration_model::Vertex);
	}

	int FindMesh(int surfaceIndex)
	{
		return mModel.FindMesh(surfaceIndex);
	}

	int AddMesh(dxr_acceleration_model::meshType_t meshType, int surfaceIndex = -1, int width=32, int height=32)
	{
		int id = mModel.MeshCount();
		mModel.AddMesh(meshType, surfaceIndex, width, height);

		return id;
	}

	void ResetMeshForUpdating(int i)
	{
		mModel.ResetMeshForUpdating(i);
	}

	dxr_acceleration_model::meshType_t GetMeshType(int i)
	{
		return mModel.GetMeshType(i);
	}

	void AddInstance(int i);
	void AddWorldInstance(int i);
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

	void WriteMeshDebug(int i, InstancesTransform& transform);
	void WriteAllTopLevelMeshsDebug();
	void WriteAllBottomLevelMeshsDebug();
	
	dxr_acceleration_model& GetModel() { return mModel; }

private:

	dxr_acceleration_model::Vertex MatrixMul(InstancesTransform& transform, dxr_acceleration_model::Vertex vecIn);

	bool mBuildOnce;

	dxr_acceleration_model mModel;
	std::vector<int> mTopLevelInstances;
	std::vector<int> mTopLevelWorldInstances;

	std::vector<InstancesTransform> mTopLevelInstanceTransforms;

	int mVerticesWritenDebug;
};
