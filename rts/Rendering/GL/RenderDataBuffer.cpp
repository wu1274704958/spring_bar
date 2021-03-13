/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <cassert>
#include <cstring> // memcpy

#include "lib/fmt/printf.h"

#include "myGL.h"
#include "System/StringUtil.h"
#include "RenderDataBuffer.hpp"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/Shaders/ShaderHandler.h"

#include "System/Log/ILog.h"


constexpr const char* stdShaderTmplVert = R"(
#version 410 core
//TODO: switch spring to 420?

#extension GL_ARB_explicit_attrib_location : require //core since 3.3
#extension GL_ARB_uniform_buffer_object    : require //core since 3.1
#extension GL_ARB_shading_language_420pack : require //core since 4.2

//defines
#define VA_TYPE {VA_TYPE}

#define SHDR_TRANSFORM_UBO          -1
#define SHDR_TRANSFORM_UNIFORM       0
#define SHDR_TRANSFORM_CAM_PLAYER    1
#define SHDR_TRANSFORM_CAM_PLAYERBB  2
#define SHDR_TRANSFORM_CAM_SHADOW    3
#define SHDR_TRANSFORM_SCREEN        4
#define SHDR_TRANSFORM_ORTHO01       5
#define SHDR_TRANSFORM_MMWORLD       6
#define SHDR_TRANSFORM_MMHM          7
#define SHDR_TRANSFORM_MMDIM         8

{DEFINES}

//globals
{GLOBALS}

//UBOs
layout(std140, binding = 0) uniform UniformMatrixBuffer {
	mat4 screenView;
	mat4 screenProj;
	mat4 screenViewProj;

	mat4 cameraView;
	mat4 cameraProj;
	mat4 cameraViewProj;
	mat4 cameraBillboardProj;

	mat4 cameraViewInv;
	mat4 cameraProjInv;
	mat4 cameraViewProjInv;

	mat4 shadowView;
	mat4 shadowProj;
	mat4 shadowViewProj;

	mat4 orthoProj01;

	mat4 mmDrawView; //world to MM
	mat4 mmDrawIMMView; //heightmap to MM
	mat4 mmDrawDimView; //mm dims

	mat4 mmDrawProj; //world to MM
	mat4 mmDrawIMMProj; //heightmap to MM
	mat4 mmDrawDimProj; //mm dims

	mat4 mmDrawViewProj; //world to MM
	mat4 mmDrawIMMViewProj; //heightmap to MM
	mat4 mmDrawDimViewProj; //mm dims
};

layout(std140, binding = 2) uniform FixedStateMatrices {
	mat4 modelViewMat;
	mat4 projectionMat;
	mat4 textureMat;
	mat4 modelViewProjectionMat;
};

//uniforms
uniform  int u_tran_sel = SHDR_TRANSFORM_UNIFORM;
uniform mat4 u_movi_mat = mat4(1.0);
uniform mat4 u_proj_mat = mat4(1.0);

// VS input attributes
{INPUTS}

// VS output attributes
{OUTPUTS}

void Transform_UBO(vec4 vertex) {
	gl_Position = modelViewProjectionMat * vertex;
}

void Transform_Uniform(vec4 vertex) {
	gl_Position = u_proj_mat * u_movi_mat * vertex;
}

void Transform_CamPlayer(vec4 vertex) {
	gl_Position = cameraViewProj * vertex;
}

void Transform_CamPlayerBB(vec4 vertex) {
	gl_Position = cameraBillboardProj * vertex;
}

void Transform_CamShadow(vec4 vertex) {
	gl_Position = shadowViewProj * vertex;
}

void Transform_Screen(vec4 vertex) {
	gl_Position = screenViewProj * vertex;
}

void Transform_Orth01(vec4 vertex) {
	gl_Position = orthoProj01 * vertex;
}

void Transform_MMWorld(vec4 vertex) {
	gl_Position = mmDrawViewProj * vertex;
}

void Transform_MMHM(vec4 vertex) {
	gl_Position = mmDrawIMMViewProj * vertex;
}

void Transform_MMDim(vec4 vertex) {
	gl_Position = mmDrawDimViewProj * vertex;
}

void Transform(vec4 vertex) {
	switch (u_tran_sel) {
	case SHDR_TRANSFORM_UBO:
		Transform_UBO(vertex);
		break;
	case SHDR_TRANSFORM_UNIFORM:
		Transform_Uniform(vertex);
		break;
	case SHDR_TRANSFORM_CAM_PLAYER:
		Transform_CamPlayer(vertex);
		break;
	case SHDR_TRANSFORM_CAM_PLAYERBB:
		Transform_CamPlayerBB(vertex);
		break;
	case SHDR_TRANSFORM_CAM_SHADOW:
		Transform_CamShadow(vertex);
		break;
	case SHDR_TRANSFORM_SCREEN:
		Transform_Screen(vertex);
		break;
	case SHDR_TRANSFORM_ORTHO01:
		Transform_Orth01(vertex);
		break;
	case SHDR_TRANSFORM_MMWORLD:
		Transform_MMWorld(vertex);
		break;
	case SHDR_TRANSFORM_MMHM:
		Transform_MMHM(vertex);
		break;
	case SHDR_TRANSFORM_MMDIM:
		Transform_MMDim(vertex);
		break;
	default:
		return;
	};
}

///
void main() {
	Transform({A_VERTEX});
{BODY}
}
)";

constexpr const char* stdShaderTmplFrag = R"(
#version 410 core
//TODO: switch spring to 420?

#extension GL_ARB_explicit_attrib_location : require //core since 3.3
//#extension GL_ARB_uniform_buffer_object    : require //core since 3.1
//#extension GL_ARB_shading_language_420pack : require //core since 4.2

//defines
#define VA_TYPE {VA_TYPE}
{DEFINES}

//globals
{GLOBALS}

//uniforms
uniform sampler2D u_tex0;
uniform vec4 u_alpha_test_ctrl = vec4(0.0, 0.0, 0.0, 1.0);
uniform float u_gamma_exp = 1.0;


// FS input attributes
{INPUTS}

// FS output attributes
layout(location = 0) out vec4 f_color_rgba;

void main() {
{BODY}
	float alpha_test_gt = float(f_color_rgba.a > u_alpha_test_ctrl.x) * u_alpha_test_ctrl.y;
	float alpha_test_lt = float(f_color_rgba.a < u_alpha_test_ctrl.x) * u_alpha_test_ctrl.z;
	if ((alpha_test_gt + alpha_test_lt + u_alpha_test_ctrl.w) == 0.0)
		discard;

	f_color_rgba.rgb = pow(f_color_rgba.rgb, vec3(u_gamma_exp));
}
)";


// global general-purpose buffers
static GL::RenderDataBuffer gRenderBuffer0 [GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer gRenderBufferC [GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer gRenderBufferFC[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer gRenderBufferT [GL::NUM_RENDER_BUFFERS];

static GL::RenderDataBuffer gRenderBufferT4[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer gRenderBufferTN[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer gRenderBufferTC[GL::NUM_RENDER_BUFFERS];

static GL::RenderDataBuffer gRenderBuffer2D0[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer gRenderBuffer2DT[GL::NUM_RENDER_BUFFERS];

static GL::RenderDataBuffer gRenderBufferL[GL::NUM_RENDER_BUFFERS];

// typed special-purpose buffer wrappers
static GL::RenderDataBuffer0 tRenderBuffer0 [GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBufferC tRenderBufferC [GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBufferC tRenderBufferFC[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBufferT tRenderBufferT [GL::NUM_RENDER_BUFFERS];

static GL::RenderDataBufferT4 tRenderBufferT4[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBufferTN tRenderBufferTN[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBufferTC tRenderBufferTC[GL::NUM_RENDER_BUFFERS];

static GL::RenderDataBuffer2D0 tRenderBuffer2D0[GL::NUM_RENDER_BUFFERS];
static GL::RenderDataBuffer2DT tRenderBuffer2DT[GL::NUM_RENDER_BUFFERS];

static GL::RenderDataBufferL tRenderBufferL[GL::NUM_RENDER_BUFFERS];


GL::RenderDataBuffer0* GL::GetRenderBuffer0 () { return (tRenderBuffer0 [0].Wait(), &tRenderBuffer0 [0]); }
GL::RenderDataBufferC* GL::GetRenderBufferC () { return (tRenderBufferC [0].Wait(), &tRenderBufferC [0]); }
GL::RenderDataBufferC* GL::GetRenderBufferFC() { return (tRenderBufferFC[0].Wait(), &tRenderBufferFC[0]); }
GL::RenderDataBufferT* GL::GetRenderBufferT () { return (tRenderBufferT [0].Wait(), &tRenderBufferT [0]); }

GL::RenderDataBufferT4* GL::GetRenderBufferT4() { return (tRenderBufferT4[0].Wait(), &tRenderBufferT4[0]); }
GL::RenderDataBufferTN* GL::GetRenderBufferTN() { return (tRenderBufferTN[0].Wait(), &tRenderBufferTN[0]); }
GL::RenderDataBufferTC* GL::GetRenderBufferTC() { return (tRenderBufferTC[0].Wait(), &tRenderBufferTC[0]); }

GL::RenderDataBuffer2D0* GL::GetRenderBuffer2D0() { return (tRenderBuffer2D0[0].Wait(), &tRenderBuffer2D0[0]); }
GL::RenderDataBuffer2DT* GL::GetRenderBuffer2DT() { return (tRenderBuffer2DT[0].Wait(), &tRenderBuffer2DT[0]); }

GL::RenderDataBufferL* GL::GetRenderBufferL() { return (tRenderBufferL[0].Wait(), &tRenderBufferL[0]); }


void GL::InitRenderBuffers() {
	Shader::GLSLShaderObject shaderObjs[2] = {{GL_VERTEX_SHADER, "", ""}, {GL_FRAGMENT_SHADER, "", ""}};


	#define MAKE_NAME_STR(T) GL::RenderDataBuffer::GetShaderName(#T)
	#define SETUP_RBUFFER(T, i, ne, ni) tRenderBuffer ## T[i].Setup(&gRenderBuffer ## T[i], &GL::VA_TYPE_ ## T ## _ATTRS, ne, ni)
	#define CREATE_SHADER(T, i, VS_CODE, FS_CODE)                                                                                            \
		do {                                                                                                                                 \
			std::string vsSrc = GL::RenderDataBuffer::FormatShader ## T("", "", VS_CODE, "VS");                                              \
			std::string fsSrc = GL::RenderDataBuffer::FormatShader ## T("", "", FS_CODE, "FS");                                              \
			shaderObjs[0] = {GL_VERTEX_SHADER  , vsSrc.c_str(), ""};                                                                         \
			shaderObjs[1] = {GL_FRAGMENT_SHADER, fsSrc.c_str(), ""};                                                                         \
			gRenderBuffer ## T[i].CreateShader((sizeof(shaderObjs) / sizeof(shaderObjs[0])), 0,  &shaderObjs[0], nullptr, MAKE_NAME_STR(T)); \
		} while (false)

	for (int i = 0; i < GL::NUM_RENDER_BUFFERS; i++) {
		SETUP_RBUFFER( 0, i, 1 << 16, 1 << 16); // InfoTexture only
		SETUP_RBUFFER( C, i, 1 << 20, 1 << 20);
		SETUP_RBUFFER(FC, i, 1 << 10, 1 << 10); // GuiHandler only
		SETUP_RBUFFER( T, i, 1 << 20, 1 << 20);

		SETUP_RBUFFER(T4, i, 1 << 16, 1 << 16); // BumpWater only
		SETUP_RBUFFER(TN, i, 1 << 16, 1 << 16); // FarTexHandler only
		SETUP_RBUFFER(TC, i, 1 << 20, 1 << 20);

		SETUP_RBUFFER(2D0, i, 1 << 16, 1 << 16); // unused
		SETUP_RBUFFER(2DT, i, 1 << 20, 1 << 20); // BumpWater,GeomBuffer

		SETUP_RBUFFER(L, i, 1 << 22, 1 << 22); // LuaOpenGL only
	}

	for (int i = 0; i < GL::NUM_RENDER_BUFFERS; i++) {
		CREATE_SHADER( 0, i, "", "\tf_color_rgba = vec4(1.0, 1.0, 1.0, 1.0);\n");
		CREATE_SHADER( C, i, "", "\tf_color_rgba = v_color_rgba;\n");
		CREATE_SHADER(FC, i, "", "\tf_color_rgba = v_color_rgba_flat;\n");
		CREATE_SHADER( T, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st);\n");

		CREATE_SHADER(T4, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_stuv.st);\n");
		CREATE_SHADER(TN, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st);\n");
		CREATE_SHADER(TC, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st) * v_color_rgba;\n");

		CREATE_SHADER(2D0, i, "", "\tf_color_rgba = vec4(1.0, 1.0, 1.0, 1.0);\n");
		CREATE_SHADER(2DT, i, "", "\tf_color_rgba = texture(u_tex0, v_texcoor_st);\n");

		// Lua buffer users are expected to supply their own
		// CREATE_SHADER(L, i, "", "\tf_color_rgba = vec4(1.0, 1.0, 1.0, 1.0);\n");
	}

	#undef CREATE_SHADER
	#undef SETUP_RBUFFER
	#undef MAKE_NAME_STR
}

void GL::KillRenderBuffers() {
	for (int i = 0; i < GL::NUM_RENDER_BUFFERS; i++) {
		gRenderBuffer0 [i].Kill();
		gRenderBufferC [i].Kill();
		gRenderBufferFC[i].Kill();
		gRenderBufferT [i].Kill();

		gRenderBufferT4[i].Kill();
		gRenderBufferTN[i].Kill();
		gRenderBufferTC[i].Kill();

		gRenderBuffer2D0[i].Kill();
		gRenderBuffer2DT[i].Kill();

		gRenderBufferL[i].Kill();
	}
}

void GL::SwapRenderBuffers() {
	static_assert(GL::NUM_RENDER_BUFFERS == 2 || GL::NUM_RENDER_BUFFERS == 3, "");

	#if (SYNC_RENDER_BUFFERS == 1)
	{
		tRenderBuffer0 [0].Sync();
		tRenderBufferC [0].Sync();
		tRenderBufferFC[0].Sync();
		tRenderBufferT [0].Sync();

		tRenderBufferT4[0].Sync();
		tRenderBufferTN[0].Sync();
		tRenderBufferTC[0].Sync();

		tRenderBuffer2D0[0].Sync();
		tRenderBuffer2DT[0].Sync();

		tRenderBufferL[0].Sync();
	}
	#endif

	// NB: called before drawFrame counter is incremented
	// A=0,B=1,C=2 -> B=0,C=1,A=2 -> C=0,A=1,B=2 -> A,B,C
	for (int i = 0, n = GL::NUM_RENDER_BUFFERS - 1; i < n; i++) {
		std::swap(tRenderBuffer0 [i], tRenderBuffer0 [i + 1]);
		std::swap(tRenderBufferC [i], tRenderBufferC [i + 1]);
		std::swap(tRenderBufferFC[i], tRenderBufferFC[i + 1]);
		std::swap(tRenderBufferT [i], tRenderBufferT [i + 1]);

		std::swap(tRenderBufferT4[i], tRenderBufferT4[i + 1]);
		std::swap(tRenderBufferTN[i], tRenderBufferTN[i + 1]);
		std::swap(tRenderBufferTC[i], tRenderBufferTC[i + 1]);

		std::swap(tRenderBuffer2D0[i], tRenderBuffer2D0[i + 1]);
		std::swap(tRenderBuffer2DT[i], tRenderBuffer2DT[i + 1]);

		std::swap(tRenderBufferL[i], tRenderBufferL[i + 1]);
	}

	{
		tRenderBuffer0 [0].Reset();
		tRenderBufferC [0].Reset();
		tRenderBufferFC[0].Reset();
		tRenderBufferT [0].Reset();

		tRenderBufferT4[0].Reset();
		tRenderBufferTN[0].Reset();
		tRenderBufferTC[0].Reset();

		tRenderBuffer2D0[0].Reset();
		tRenderBuffer2DT[0].Reset();

		tRenderBufferL[0].Reset();
	}

	CglFont::SwapRenderBuffers();
}




void GL::RenderDataBuffer::EnableAttribs(size_t numAttrs, const Shader::ShaderInput* rawAttrs) const {
	for (size_t n = 0; n < numAttrs; n++) {
		const Shader::ShaderInput& a = rawAttrs[n];

		glEnableVertexAttribArray(a.index);
		glVertexAttribPointer(a.index, a.count, a.type, a.normalize, a.stride, a.data);
	}
}

void GL::RenderDataBuffer::DisableAttribs(size_t numAttrs, const Shader::ShaderInput* rawAttrs) const {
	for (size_t n = 0; n < numAttrs; n++) {
		glDisableVertexAttribArray(rawAttrs[n].index);
	}
}


std::string GL::RenderDataBuffer::FormatShaderBase(
	const char* defines,
	const char* globals,
	const char* type,
	const char* name
) {

	std::string shaderSrc;

	if (type[0] == 'V') {
		//shaderSrc = Shader::GetShaderSource("GLSL/StdShaderTmplVert.glsl");
		shaderSrc = stdShaderTmplVert;
	}
	else if (type[0] == 'F') {
		//shaderSrc = Shader::GetShaderSource("GLSL/StdShaderTmplFrag.glsl");
		shaderSrc = stdShaderTmplFrag;
	}

	if (shaderSrc.empty())
		return "";

	shaderSrc = StringReplace(shaderSrc, "{VA_TYPE}", type);
	shaderSrc = StringReplace(shaderSrc, "{DEFINES}", defines);
	shaderSrc = StringReplace(shaderSrc, "{GLOBALS}", globals);

	return shaderSrc;
}

std::string GL::RenderDataBuffer::FormatShaderType(
	std::string& shaderSrc,
	size_t numAttrs,
	const Shader::ShaderInput* rawAttrs,
	const char* code,
	const char* type,
	const char* name
) {

	constexpr const char*  vecTypes[] = {"vec2", "vec3", "vec4"};
	constexpr const char* typeQuals[] = {"", "flat"};

	constexpr const char* vsInpFmt = "layout(location = %d) in %s %s;\n";
	constexpr const char* varyiFmt = "%s %s %s v_%s;\n";
	constexpr const char* vsBdyFmt = "\tv_%s = %s;\n";

	if (type[0] == 'V') {
		// position (2D, 3D, or 4D [Lua]) is always the first attribute
		switch (rawAttrs[0].count) {
			case 2: { shaderSrc = StringReplace(shaderSrc, "{A_VERTEX}", "vec4(a_vertex_xy  , 0.0, 1.0)"); } break;
			case 3: { shaderSrc = StringReplace(shaderSrc, "{A_VERTEX}", "vec4(a_vertex_xyz ,      1.0)"); } break;
			case 4: { shaderSrc = StringReplace(shaderSrc, "{A_VERTEX}", "vec4(a_vertex_xyzw          )"); } break;
			default: {} break;
		}
	}

	std::stringstream inpuBody;
	std::stringstream outpBody;
	std::stringstream shdrBody;

	for (size_t n = 0; n < numAttrs; n++) {
		const Shader::ShaderInput& a = rawAttrs[n];

		assert(a.count >= 2);
		assert(a.count <= 4);

		const char*  vecType = vecTypes[a.count - 2];
		const char* typeQual = typeQuals[strstr(a.name + 2, typeQuals[1]) != nullptr];

		switch (type[0]) {
		case 'V': {
			inpuBody << fmt::sprintf(vsInpFmt, a.index, vecType, a.name);
			outpBody << fmt::sprintf(varyiFmt, typeQual, "out", vecType, a.name + 2);
			shdrBody << fmt::sprintf(vsBdyFmt, a.name + 2, a.name);
		} break;
		case 'F': {
			inpuBody << fmt::sprintf(varyiFmt, typeQual, "in" , vecType, a.name + 2);
			//outAttrs << fsOut; //stays empty
		} break;
		default: {} break;
		}
	}

	if (code[0] != '\0') { //supplied specific code
		shdrBody << code;
	}

	const auto replaceFunc = [&shaderSrc](const char* what, const std::string& with) {
		if (!with.empty())
			shaderSrc = StringReplace(shaderSrc, what, with);
	};

	replaceFunc("{INPUTS}" , inpuBody.str());
	replaceFunc("{OUTPUTS}", outpBody.str());
	replaceFunc("{BODY}"   , shdrBody.str());

	//LOG("inpuBody:\n%s", inpuBody.str().c_str());
	//LOG("Shader src:\n%s", shaderSrc.c_str());

	return shaderSrc;
}


Shader::GLSLProgramObject* GL::RenderDataBuffer::CreateShader(
	size_t numObjects,
	size_t numUniforms,
	Shader::GLSLShaderObject* objects,
	const Shader::ShaderInput* uniforms,
	const char* progName
) {
	for (size_t n = 0; n < numObjects; n++) {
		shader.AttachShaderObject(&objects[n]);
	}

	// keep the source strings around for LuaOpenGL
	if (progName[0] != 0)
		shaderHandler->InsertExtProgramObject(progName, &shader);

	shader.ReloadShaderObjects();
	shader.CreateAndLink();
	shader.RecalculateShaderHash();
	// RDB shaders are never reloaded, get rid of attachments early
	shader.ClearAttachedShaderObjects();

	for (size_t n = 0; n < numUniforms; n++) {
		shader.SetUniform(uniforms[n]);
	}

	shader.Validate();
	return &shader;
}

void GL::RenderDataBuffer::SetMatrixStackMode(Shader::IProgramObject* shader, const ShaderTransformType shtt)
{
	assert(shader);
	assert(shader->GetUniformLoc("u_tran_sel") >= 0);

	bool wasBound = shader->IsBound();

	if (!wasBound)
		shader->Enable();

	shader->SetUniform("u_tran_sel", static_cast<int>(shtt));

	if (!wasBound)
		shader->Disable();
}

void GL::RenderDataBuffer::Upload(
	size_t numElems,
	size_t numIndcs,
	size_t numAttrs,
	const uint8_t* rawElems,
	const uint8_t* rawIndcs,
	const Shader::ShaderInput* rawAttrs
) {
	array.Bind();
	elems.Bind();
	elems.New(numElems * sizeof(uint8_t), elems.GetUsage(), rawElems);

	if (numIndcs > 0) {
		indcs.Bind();
		indcs.New(numIndcs * sizeof(uint8_t), indcs.GetUsage(), rawIndcs);
	}

	EnableAttribs(numAttrs, rawAttrs);

	array.Unbind();
	elems.Unbind();

	if (numIndcs > 0)
		indcs.Unbind();

	DisableAttribs(numAttrs, rawAttrs);
}


void GL::RenderDataBuffer::Submit(uint32_t primType, uint32_t dataIndx, uint32_t dataSize) const {
	assert(elems.GetSize() != 0);
	// buffers populated with (dummy or actual) indices
	// can still be Submit()'ed for non-indexed drawing
	assert(indcs.GetSize() >= 0);

	array.Bind();

	// dataIndx := first elem, dataSize := numElems (unique verts)
	glDrawArrays(primType, dataIndx, dataSize);

	array.Unbind();
}

void GL::RenderDataBuffer::SubmitInstanced(uint32_t primType, uint32_t dataIndx, uint32_t dataSize, uint32_t numInsts) const {
	array.Bind();
	glDrawArraysInstanced(primType, dataIndx, dataSize, numInsts);
	array.Unbind();
}


void GL::RenderDataBuffer::SubmitIndexed(uint32_t primType, uint32_t dataIndx, uint32_t dataSize) const {
	assert(elems.GetSize() != 0);
	assert(indcs.GetSize() != 0);

	array.Bind();

	// dataIndx := index offset, dataSize := numIndcs
	glDrawElements(primType, dataSize, GL_UNSIGNED_INT, VA_TYPE_OFFSET(uint32_t, dataIndx));

	array.Unbind();
}

void GL::RenderDataBuffer::SubmitIndexedInstanced(uint32_t primType, uint32_t dataIndx, uint32_t dataSize, uint32_t numInsts) const {
	array.Bind();
	glDrawElementsInstanced(primType, dataSize, GL_UNSIGNED_INT, VA_TYPE_OFFSET(uint32_t, dataIndx), numInsts);
	array.Unbind();
}




#if 0
void GL::RenderDataBuffer::UploadC(
	size_t numElems,
	size_t numIndcs,
	const VA_TYPE_C* rawElems,
	const uint32_t* rawIndcs
) {
	uploadBuffer.clear();
	uploadBuffer.reserve(numElems * (VA_SIZE_C + 3));

	for (size_t n = 0; n < numElems; n++) {
		const VA_TYPE_C& e = rawElems[n];

		uploadBuffer.push_back(e.p.x);
		uploadBuffer.push_back(e.p.y);
		uploadBuffer.push_back(e.p.z);
		uploadBuffer.push_back(e.c.r); // turn SColor uint32 into 4 floats
		uploadBuffer.push_back(e.c.g);
		uploadBuffer.push_back(e.c.b);
		uploadBuffer.push_back(e.c.a);
	}

	Upload(uploadBuffer.size(), numIndcs, NUM_VA_TYPE_C_ATTRS, uploadBuffer.data(), rawIndcs, VA_TYPE_C_ATTRS);
}
#endif

