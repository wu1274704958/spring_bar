/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef GL_MATRIX_STACK_UPLOADER_H
#define GL_MATRIX_STACK_UPLOADER_H

#include <array>
#include "System/Matrix44f.h"
#include "Rendering/GL/VBO.h"

namespace GL {
	class MatrixStateUploader {
	public:
		static constexpr bool enabled = true;
		bool Supported()
		{
			static bool supported = enabled && VBO::IsSupported(GL_UNIFORM_BUFFER) && GLEW_ARB_shading_language_420pack; //UBO && UBO layout(binding=x)
			return supported;
		}
		static MatrixStateUploader& GetInstance() {
			static MatrixStateUploader instance;
			return instance;
		};
	public:
		void Init();
		void Kill();
		void Upload(const unsigned int updateElemOffset, const CMatrix44f& mat);
	private:
		void InitVBO();
		void KillVBO();
	private:
		constexpr static uint32_t MATRIX_STATE_UBO_INDEX = 2;
	private:
		bool initialized = false;
		std::array<CMatrix44f, 4> matrixStateArray; // M*V, P, T, M*V*P
		VBO* ubo;
	};
};

#endif //GL_MATRIX_STACK_UPLOADER_H