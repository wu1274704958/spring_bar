/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "HeightMapTexture.h"

#include "ReadMap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/TexBind.h"
#include "System/EventHandler.h"
#include "System/Rectangle.h"
#include "System/TimeProfiler.h"

#include <cstring>

HeightMapTexture* heightMapTexture = nullptr;
HeightMapTexture::HeightMapTexture(): CEventClient("[HeightMapTexture]", 2718965, false)
{
	eventHandler.AddClient(this);
	Init();
}


HeightMapTexture::~HeightMapTexture()
{
	Kill();
	eventHandler.RemoveClient(this);
}


void HeightMapTexture::Init()
{
	assert(readMap != nullptr);

	// corner-heightmap dimensions
	xSize = mapDims.mapxp1;
	ySize = mapDims.mapyp1;

	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if constexpr (ExternalFormat == GL_RED) {
		constexpr GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_RED };
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
	}

	glTexImage2D(GL_TEXTURE_2D, 0, InternalFormat,
		xSize, ySize, 0,
		ExternalFormat, DataType, readMap->GetCornerHeightMapUnsynced());

	GLint allocWidth, allocHeight, allocIntFormat, allocRedType;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &allocWidth);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &allocHeight);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &allocIntFormat);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_TYPE, &allocRedType);
	if (std::tie(allocWidth, allocHeight, allocIntFormat, allocRedType) != std::make_tuple(xSize, ySize, InternalFormat, DataType)) {
		LOG_L(L_ERROR, "HeightMapTexture::Init(). Failed to allocate texture (w=%d, h=%d, if=%d, rt=%d)", allocWidth, allocHeight, allocIntFormat, allocRedType);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}


void HeightMapTexture::Kill()
{
	glDeleteTextures(1, &texID);

	texID = 0;
	xSize = 0;
	ySize = 0;

	for (PBO& pbo: pbos) {
		pbo.Release();
	}
}


void HeightMapTexture::UnsyncedHeightMapUpdate(const SRectangle& rect)
{
	if (texID == 0)
		return;

	SCOPED_TIMER("HeightMapTexture::UHMU");

	// the upper bounds of UHM rectangles are clamped to
	// map{x,y}; valid for indexing the corner heightmap
	const int sizeX = rect.GetWidth() + 1;
	const int sizeZ = rect.GetHeight() + 1;

	assert(sizeX <= xSize);
	assert(sizeZ <= ySize);

	// RR update policy
	PBO& pbo = pbos[globalRendering->drawFrame % 3];

	pbo.Bind();
	pbo.New(sizeX * sizeZ * sizeof(float));

	const float* heightMap = readMap->GetCornerHeightMapUnsynced();
	      float* heightBuf = reinterpret_cast<float*>(pbo.MapBuffer(0, pbo.GetSize(), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | pbo.mapUnsyncedBit));

	if (heightBuf != nullptr) {
		for (int z = 0; z < sizeZ; z++) {
			const void* src = heightMap + rect.x1 + (z + rect.z1) * xSize;
			      void* dst = heightBuf +           (z          ) * sizeX;

			memcpy(dst, src, sizeX * sizeof(float));
		}
	}
	else {
		LOG_L(L_ERROR, "HeightMapTexture::UHMU(). Failed to allocate heightBuf");
	}

	pbo.UnmapBuffer();

	if (heightBuf) {
		auto texBind = GL::TexBind(GL_TEXTURE_2D, texID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x1, rect.z1, sizeX, sizeZ, ExternalFormat, DataType, pbo.GetPtr());
		if (auto err = glGetError(); err != GL_NO_ERROR) {
			LOG_L(L_ERROR, "HeightMapTexture::UHMU(). Failed to update texture (err=%u)", err);
		}
	}

	pbo.Invalidate();
	pbo.Unbind();
	//pbo.Release();
}
