#include "tr_local.h"

#include "dxr_acceleration_structure_manager.h"

dxr_acceleration_structure_manager::dxr_acceleration_structure_manager()
{
}

dxr_acceleration_structure_manager::~dxr_acceleration_structure_manager()
{
}

void dxr_acceleration_structure_manager::Reset()
{
	mTopLevelInstances.clear();

	mTopLevelInstanceTransforms.clear();
	mModel.Reset();

}

void dxr_acceleration_structure_manager::AddInstance(int i)
{
	mTopLevelInstances.push_back(i);

	InstancesTransform transform;
	memset(&transform.data, 0, sizeof(FLOAT) * 3 * 4);
	transform.data[0][0] = transform.data[1][1] = transform.data[2][2] = 1;		// identity transform

	mTopLevelInstanceTransforms.push_back(transform);
}

void dxr_acceleration_structure_manager::AddInstanceWithTransform(int i, vec3_t axis[3], float origin[3])
{
	mTopLevelInstances.push_back(i);

	InstancesTransform transform;

	for (int i = 0; i < 3; ++i)
	{
		transform.data[0][i] = axis[i][0];
		transform.data[1][i] = axis[i][1];
		transform.data[2][i] = axis[i][2];
	}
	transform.data[0][3] = origin[0];
	transform.data[1][3] = origin[1];
	transform.data[2][3] = origin[2];

	mTopLevelInstanceTransforms.push_back(transform);
}

int dxr_acceleration_structure_manager::EndFrame()
{
	mTopLevelInstances.clear();
	mTopLevelInstanceTransforms.clear();

	if (MeshCount() > 0)
	{
		AddInstance(0);
	}

	return 0;
}

void dxr_acceleration_structure_manager::WriteMeshDebug(int n, InstancesTransform& transform)
{
	Model::Mesh& m = mModel.meshes[n];

	char buffer[512];
	sprintf(buffer, "o mesh.%d\n", n);	OutputDebugStringA(buffer);
	sprintf(buffer, "g mesh.%d\n", n);	OutputDebugStringA(buffer);
	sprintf(buffer, "usemtl mesh.%d\n", n);	OutputDebugStringA(buffer);

	//write veteics
	for (int i = m.StartVertexIndex; i < m.StartVertexIndex + m.VertexCount; ++i)
	{
		Model::Vertex ver = MatrixMul(transform, mModel.vertices[i]);
		sprintf(buffer, "v %f %f %f\n", ver.position.x, ver.position.y, ver.position.z);	OutputDebugStringA(buffer);
	}

	//write normals
	for (int i = m.StartVertexIndex; i < m.StartVertexIndex + m.VertexCount; ++i)
	{
		sprintf(buffer, "vn %f %f %f\n", mModel.vertices[i].normal.x, mModel.vertices[i].normal.y, mModel.vertices[i].normal.z);	OutputDebugStringA(buffer);
	}
	
	sprintf(buffer, "s %d\n", n);	OutputDebugStringA(buffer);

	for (int i = m.StartIndexIndex; i < m.StartIndexIndex + m.IndexCount; i += 3)
	{
		int a = mModel.indices[i + 0] + 1 + mVerticesWritenDebug;
		int b = mModel.indices[i + 1] + 1 + mVerticesWritenDebug;
		int c = mModel.indices[i + 2] + 1 + mVerticesWritenDebug;

		sprintf(buffer, "f %d//%d %d//%d %d//%d\n", a, a, b, b, c, c);	OutputDebugStringA(buffer);
	}

	mVerticesWritenDebug += m.VertexCount;
}

void dxr_acceleration_structure_manager::WriteAllTopLevelMeshsDebug()
{
	mVerticesWritenDebug = 0;
	int count = GetInstanceCount();
	for (int i = 0; i < count; ++i)
	{
		int j = mTopLevelInstances[i];
		InstancesTransform& transform = mTopLevelInstanceTransforms[i];
		WriteMeshDebug(j, transform);
	}
}

void dxr_acceleration_structure_manager::WriteAllBottomLevelMeshsDebug()
{
	InstancesTransform transform;
	memset(&transform.data, 0, sizeof(FLOAT) * 3 * 4);
	transform.data[0][0] = transform.data[1][1] = transform.data[2][2] = 1;		// identity transform

	mVerticesWritenDebug = 0;
	int count = MeshCount();
	for (int i = 0; i < count; ++i)
	{
		WriteMeshDebug(i, transform);
	}
}

dxr_acceleration_structure_manager::Model::Vertex dxr_acceleration_structure_manager::MatrixMul(dxr_acceleration_structure_manager::InstancesTransform& transform, dxr_acceleration_structure_manager::Model::Vertex vecIn)
{
	dxr_acceleration_structure_manager::Model::Vertex vecOut;

	vecOut.normal.x = vecIn.normal.x;
	vecOut.normal.y = vecIn.normal.y;
	vecOut.normal.z = vecIn.normal.z;

	vecOut.position.x = (vecIn.position.x * transform.data[0][0]) + (vecIn.position.y * transform.data[0][1]) + (vecIn.position.z * transform.data[0][2]) + (1.0f * transform.data[0][3]);
	vecOut.position.y = (vecIn.position.x * transform.data[1][0]) + (vecIn.position.y * transform.data[1][1]) + (vecIn.position.z * transform.data[1][2]) + (1.0f * transform.data[1][3]);
	vecOut.position.z = (vecIn.position.x * transform.data[2][0]) + (vecIn.position.y * transform.data[2][1]) + (vecIn.position.z * transform.data[2][2]) + (1.0f * transform.data[2][3]);

	return vecOut;
}