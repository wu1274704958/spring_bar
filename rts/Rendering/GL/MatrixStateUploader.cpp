/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "MatrixStateUploader.hpp"

#include "Rendering/GL/myGL.h"
#include "System/SafeUtil.h"

using namespace GL;

void MatrixStateUploader::InitVBO()
{
	ubo = new VBO(GL_UNIFORM_BUFFER, false, false);
	ubo->Bind(GL_UNIFORM_BUFFER);
	ubo->New(sizeof(matrixStateArray), GL_STREAM_DRAW);
	ubo->Unbind();

	ubo->BindBufferRange(GL_UNIFORM_BUFFER, MATRIX_STATE_UBO_INDEX, 0, ubo->GetSize());
}
void MatrixStateUploader::Init()
{
	if (initialized) //don't need to reinit on resolution changes
		return;

	InitVBO();

	initialized = true;
}

void MatrixStateUploader::KillVBO()
{
	if (ubo && ubo->GetIdRaw() > 0u) {
		if (ubo->bound)
			ubo->Unbind();

		ubo->UnbindBufferRange(GL_UNIFORM_BUFFER, MATRIX_STATE_UBO_INDEX, 0, ubo->GetSize());
	}

	spring::SafeDelete(ubo);
	initialized = false;
}

void MatrixStateUploader::Kill()
{
	if (!Supported() || !initialized)
		return;

	KillVBO();
}

void MatrixStateUploader::Upload(const unsigned int updateElemOffset, const CMatrix44f& mat)
{
	if (!Supported() || !initialized)
		return;

	matrixStateArray[updateElemOffset] = mat;

	if (updateElemOffset < 2) {
		matrixStateArray[3] = matrixStateArray[0] * matrixStateArray[1]; //MV * P
	}

	ubo->Bind();
	ubo->SetBufferSubData(0, sizeof(matrixStateArray), matrixStateArray.data()); //seems to be faster than mapping
	ubo->Unbind();
}
