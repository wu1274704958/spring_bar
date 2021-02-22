/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * @brief ARB_vertex_buffer_object implementation
 * ARB_vertex_buffer_object class implementation
 */

#include <cassert>
#include <vector>

#include "VBO.h"

#include "Rendering/GlobalRendering.h"
#include "System/Config/ConfigHandler.h"
#include "System/Log/ILog.h"
#include "System/SpringMath.h"

 /**
  * Returns if the current gpu drivers support object's buffer type
  */
bool VBO::IsSupported() const
{
	return VBO::IsSupported(curBoundTarget);
}

/**
 * Returns if the current gpu drivers support certain buffer type
 */
bool VBO::IsSupported(GLenum target) {
	static bool isPBOSupported = true;
	static bool isVBOSupported = true;
	static bool isUBOSupported = true;
	static bool isSSBOSupported = GLEW_ARB_shader_storage_buffer_object;
	static bool isCopyBuffSupported = true;

	switch (target) {
	case GL_PIXEL_PACK_BUFFER:
	case GL_PIXEL_UNPACK_BUFFER:
		return isPBOSupported;
	case GL_ARRAY_BUFFER:
	case GL_ELEMENT_ARRAY_BUFFER:
		return isVBOSupported;
	case GL_UNIFORM_BUFFER:
		return isUBOSupported;
	case GL_SHADER_STORAGE_BUFFER:
		return isSSBOSupported;
	case GL_COPY_WRITE_BUFFER:
	case GL_COPY_READ_BUFFER:
		return isCopyBuffSupported;
	default: {
		LOG_L(L_ERROR, "[VBO:%s]: wrong target [%u] is specified", __func__, target);
		return false;
	}
	}
}

VBO::VBO(GLenum _defTarget, const bool storage, bool readable)
{
	curBoundTarget = _defTarget;
	defTarget = _defTarget;

	immutableStorage = storage;
	readableStorage = readable;
}

VBO& VBO::operator=(VBO&& other) noexcept
{
	std::swap(vboId, other.vboId);
	std::swap(bound, other.bound);
	std::swap(mapped, other.mapped);
	std::swap(nullSizeMapped, other.nullSizeMapped);

	std::swap(bufSize, other.bufSize);
	std::swap(memSize, other.memSize);

	std::swap(curBoundTarget, other.curBoundTarget);
	std::swap(defTarget, other.defTarget);
	std::swap(usage, other.usage);
	std::swap(mapUnsyncedBit, other.mapUnsyncedBit);

	std::swap(immutableStorage, other.immutableStorage);
	std::swap(readableStorage, other.readableStorage);
	return *this;
}


void VBO::Generate() const { glGenBuffers(1, &vboId); }
void VBO::Delete() const { glDeleteBuffers(1, &vboId); vboId = 0; }

void VBO::Bind(GLenum target) const
{
	assert(!bound);
	bound = true;

	glBindBuffer(curBoundTarget = target, GetId());
}

void VBO::Unbind() const
{
	assert(bound);
	bound = false;

	glBindBuffer(curBoundTarget, 0);
}

bool VBO::BindBufferRangeImpl(GLenum target, GLuint index, GLuint _vboId, GLuint offset, GLsizeiptr size) const
{
	assert(offset + size <= bufSize);

	if (target != curBoundTarget && !VBO::IsSupported(target)) {
		LOG_L(L_ERROR, "[VBO::%s]: Unsupported buffer target [%u]", __func__, target);
		return false;
	}

	if (target != GL_UNIFORM_BUFFER && target != GL_SHADER_STORAGE_BUFFER) { //assert(?)
		LOG_L(L_ERROR, "[VBO::%s]: attempt to bind wrong target [%u]", __func__, target);
		return false;
	}

	if (target == GL_UNIFORM_BUFFER && index >= globalRendering->glslMaxUniformBufferBindings) {
		LOG_L(L_ERROR, "[VBO::%s]: attempt to bind UBO with invalid index [%u]", __func__, index);
		return false;
	}

	if (target == GL_SHADER_STORAGE_BUFFER && index >= globalRendering->glslMaxStorageBufferBindings) {
		LOG_L(L_ERROR, "[VBO::%s]: attempt to bind SSBO with invalid index [%u]", __func__, index);
		return false;
	}

	const size_t neededAlignment = GetOffsetAlignment(target);
	if (offset % neededAlignment != 0 || size % neededAlignment != 0) { //assert(?)
		LOG_L(L_ERROR, "[VBO::%s]: attempt to bind with wrong offset [%u] or size [%I64u]. Needed alignment [%I64u]", __func__, offset, size, neededAlignment);
		return false;
	}

	glBindBufferRange(target, index, _vboId, offset, size);

	BoundBufferRangeIndex bbri = { target, index };
	BoundBufferRangeData  bbrd = { offset, size };
	if (_vboId != 0) {
		bbrItems[bbri] = bbrd;
	}
	else {
		if (bbrItems[bbri] == bbrd) { //exact match of unbind call
			bbrItems.erase(bbri);
		}
	}

	return true;
}


bool VBO::Resize(GLsizeiptr newSize, GLenum newUsage)
{
	assert(bound);
	assert(!mapped);

	// no size change -> nothing to do
	if (newSize == bufSize && newUsage == usage)
		return true;

	// first call: no *BO exists yet to copy old data from, so use ::New() (faster)
	if (bufSize == 0)
		return New(newSize, newUsage, nullptr);

	const size_t oldSize = bufSize;
	const GLenum oldBoundTarget = curBoundTarget;

	glClearErrors("VBO", __func__, globalRendering->glDebugErrors);

	{
		VBO vbo(GL_COPY_WRITE_BUFFER, immutableStorage);

		vbo.Bind(GL_COPY_WRITE_BUFFER);
		vbo.New(newSize, GL_STREAM_DRAW);

		// gpu internal copy (fast)
		if (oldSize > 0)
			glCopyBufferSubData(curBoundTarget, GL_COPY_WRITE_BUFFER, 0, 0, oldSize);

		vbo.Unbind();
		Unbind();
		*this = std::move(vbo);
		Bind(oldBoundTarget);
	}

	const GLenum err = glGetError();

	if (err == GL_NO_ERROR) {
		bufSize = newSize;
		usage = newUsage;
		return true;
	}

	LOG_L(L_ERROR, "[VBO::%s(size=%lu,usage=%u)] id=%u tgt=0x%x err=0x%x", __func__, (unsigned long) bufSize, usage, vboId, curBoundTarget, err);
	Unbind();
	return false;
}


bool VBO::New(GLsizeiptr newSize, GLenum newUsage, const void* newData)
{
	assert(bound);
	assert(!mapped || (newData == nullptr && newSize == bufSize && newUsage == usage));

	// ATI interprets unsynchronized access differently; (un)mapping does not sync
	mapUnsyncedBit = GL_MAP_UNSYNCHRONIZED_BIT * (1 - globalRendering->haveAMD);

	// no-op new, allows e.g. repeated Bind+New with persistent buffers
	if (newData == nullptr && newSize == bufSize && newUsage == usage)
		return true;

	if (immutableStorage && bufSize != 0) {
		LOG_L(L_ERROR, "[VBO::%s({cur,new}size={" _STPF_ "," _STPF_ "},{cur,new}usage={0x%x,0x%x},data=%p)] cannot recreate persistent storage buffer", __func__, bufSize, newSize, usage, newUsage, newData);
		return false;
	}


	glClearErrors("VBO", __func__, globalRendering->glDebugErrors);

	#ifdef GLEW_ARB_buffer_storage
	if (immutableStorage) {
		glBufferStorage(curBoundTarget, newSize, newData, /*newUsage =*/ (GL_MAP_READ_BIT * readableStorage) | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT);
	} else
	#endif
	{
		glBufferData(curBoundTarget, newSize, newData, newUsage);
	}

	const GLenum err = glGetError();

	if (err == GL_NO_ERROR) {
		bufSize = newSize;
		usage = newUsage;
		return true;
	}

	LOG_L(L_ERROR, "[VBO::%s(size=%lu,usage=0x%x)] id=%u tgt=0x%x err=0x%x", __func__, (unsigned long) bufSize, usage, vboId, curBoundTarget, err);
	Unbind();
	return false;
}


GLubyte* VBO::MapBuffer(GLbitfield access)
{
	assert(!mapped);
	return MapBuffer(0, bufSize, access);
}


GLubyte* VBO::MapBuffer(GLintptr offset, GLsizeiptr size, GLbitfield access)
{
	assert(!mapped);
	assert((offset + size) <= bufSize);
	mapped = true;

	// glMapBuffer & glMapBufferRange use different flags for their access argument
	// for easier handling convert the glMapBuffer ones here
	switch (access) {
		case GL_WRITE_ONLY: {
			access = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | mapUnsyncedBit;

			#ifdef GLEW_ARB_buffer_storage
			access &= ~((GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT) * immutableStorage);
			access |=  ((GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT            ) * immutableStorage);
			#endif
		} break;
		case GL_READ_WRITE: {
			access = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | mapUnsyncedBit;

			#ifdef GLEW_ARB_buffer_storage
			access &= ~(GL_MAP_UNSYNCHRONIZED_BIT * immutableStorage);
			access |=  (GL_MAP_PERSISTENT_BIT     * immutableStorage);
			#endif
		} break;
		case GL_READ_ONLY: {
			access = GL_MAP_READ_BIT | mapUnsyncedBit;

			#ifdef GLEW_ARB_buffer_storage
			access &= ~(GL_MAP_UNSYNCHRONIZED_BIT * immutableStorage);
			access |=  (GL_MAP_PERSISTENT_BIT     * immutableStorage);
			#endif
		} break;

		default: break;
	}

	if (size == 0) {
		// nvidia incorrectly returns GL_INVALID_VALUE when trying to call glMapBufferRange with size zero
		// so catch it ourselves
		nullSizeMapped = true;
		return nullptr;
	}


	GLubyte* ptr = (GLubyte*) glMapBufferRange(curBoundTarget, offset, size, access);
	#ifndef HEADLESS
	assert(ptr != nullptr);
	#else
	assert(ptr == nullptr);
	#endif

	return ptr;
}


void VBO::UnmapBuffer()
{
	assert(mapped);

	if (!nullSizeMapped)
		glUnmapBuffer(curBoundTarget);

	mapped = false;
	nullSizeMapped = false;
}


void VBO::Invalidate()
{
	assert(bound);
	assert(immutableStorage || !mapped);

#ifdef GLEW_ARB_invalidate_subdata
	// OpenGL4 way
	if (GLEW_ARB_invalidate_subdata) {
		glInvalidateBufferData(GetId());
		return;
	}
#endif

	// note: allocating memory doesn't actually block the memory it just makes room in _virtual_ memory space
	New(bufSize, usage, nullptr);
}


const GLvoid* VBO::GetPtr(GLintptr offset) const
{
	assert(bound);
	return (GLvoid*)((char*)nullptr + (offset));
}

size_t VBO::GetAlignedSize(GLenum target, size_t sz)
{
	const size_t alignmentReq = GetOffsetAlignment(target);
	if (alignmentReq > 1)
		return AlignUp(sz, alignmentReq);

	return sz;
}

size_t VBO::GetOffsetAlignment(GLenum target) {

	const auto getOffsetAlignmentUBO = []() -> size_t {
		GLint buffAlignment = 0;
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &buffAlignment);
		return static_cast<size_t>(buffAlignment);
	};

	const auto getOffsetAlignmentSSBO = []() -> size_t {
		GLint buffAlignment = 0;
		glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &buffAlignment);
		return static_cast<size_t>(buffAlignment);
	};

	static size_t offsetAlignmentUBO = getOffsetAlignmentUBO();
	static size_t offsetAlignmentSSBO = getOffsetAlignmentSSBO();

	switch (target) {
	case GL_UNIFORM_BUFFER:
		return offsetAlignmentUBO;
	case GL_SHADER_STORAGE_BUFFER:
		return offsetAlignmentSSBO;
	case GL_PIXEL_PACK_BUFFER:
	case GL_PIXEL_UNPACK_BUFFER:
	case GL_ARRAY_BUFFER:
	case GL_ELEMENT_ARRAY_BUFFER:
	default:
		return 1;
	}
}