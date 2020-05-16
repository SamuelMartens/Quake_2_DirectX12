#include "dx_model.h"

Model::Model(Model&& other)
{
	if (this == &other)
	{
		return;
	}

	glModel = other.glModel;

	other.glModel = nullptr;
}

Model& Model::operator=(Model&& other)
{
	if (this == &other)
	{
		return;
	}

	glModel = other.glModel;

	other.glModel = nullptr;

	return *this;
}

Model::~Model()
{
	if (glModel == nullptr)
		return;

	Hunk_Free(glModel->extradata);

	free(glModel);
}
