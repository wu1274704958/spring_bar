/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/GL/myGL.h"
#include "Rendering/GL/PBO.h"
#include "System/EventClient.h"

class HeightMapTexture : public CEventClient
{
	public:
		//! CEventClient interface
		bool WantsEvent(const std::string& eventName) {
			return (eventName == "UnsyncedHeightMapUpdate");
		}
		bool GetFullRead() const { return true; }
		int GetReadAllyTeam() const { return AllAccessTeam; }

		void UnsyncedHeightMapUpdate(const SRectangle& rect);

	public:
		HeightMapTexture();
		~HeightMapTexture();

		GLuint GetTextureID() const { return texID; }

		int GetSizeX() const { return xSize; }
		int GetSizeY() const { return ySize; }

	private:
		void Init();
		void Kill();

	private:
		GLuint texID = 0;

		int xSize = 0;
		int ySize = 0;

		PBO pbos[3];

		static constexpr GLint ExternalFormat = GL_RED;
		static constexpr GLint InternalFormat = GL_R32F;
		static constexpr GLint DataType = GL_FLOAT;
};

extern HeightMapTexture* heightMapTexture;