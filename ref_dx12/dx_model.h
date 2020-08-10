#pragma once

#include "dx_glmodel.h"

// Very thin wrapper around GL model. Needed for RAII only right now
// Not used currently
class Model
{
public:

	Model() = default;

	Model(const Model&) = delete;
	Model& operator=(const Model&) = delete;

	Model(Model&& other);
	Model& operator=(Model&& other);

	~Model();

	model_t* glModel = nullptr;
};