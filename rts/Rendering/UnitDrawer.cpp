/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "UnitDrawer.h"
#include "UnitDrawerState.hpp"

#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "Game/Game.h"
#include "Game/GameHelper.h"
#include "Game/GameSetup.h"
#include "Game/GlobalUnsynced.h"
#include "Game/Players/Player.h"
#include "Game/UI/MiniMap.h"
#include "Map/BaseGroundDrawer.h"
#include "Map/Ground.h"
#include "Map/MapInfo.h"
#include "Map/ReadMap.h"

#include "Rendering/Env/ISky.h"
#include "Rendering/Env/IWater.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/FarTextureHandler.h"
#include "Rendering/GL/glExtra.h"
#include "Rendering/GL/VertexArray.h"
#include "Rendering/Env/IGroundDecalDrawer.h"
#include "Rendering/Colors.h"
#include "Rendering/IconHandler.h"
#include "Rendering/LuaObjectDrawer.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#include "Rendering/Textures/3DOTextureHandler.h"
#include "Rendering/Textures/S3OTextureHandler.h"

#include "Sim/Features/Feature.h"
#include "Sim/Misc/LosHandler.h"
#include "Sim/Misc/TeamHandler.h"
#include "Sim/Projectiles/ExplosionGenerator.h"
#include "Sim/Units/BuildInfo.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitHandler.h"

#include "System/EventHandler.h"
#include "System/Config/ConfigHandler.h"
#include "System/FileSystem/FileHandler.h"

#include "System/MemPoolTypes.h"
#include "System/SpringMath.h"

#define UNIT_SHADOW_ALPHA_MASKING

CONFIG(int, UnitLodDist).defaultValue(1000).headlessValue(0);
CONFIG(int, UnitIconDist).defaultValue(200).headlessValue(0);
CONFIG(float, UnitIconScaleUI).defaultValue(1.0f).minimumValue(0.5f).maximumValue(2.0f);
CONFIG(float, UnitIconFadeStart).defaultValue(3000.0f).minimumValue(1.0f).maximumValue(10000.0f);
CONFIG(float, UnitIconFadeVanish).defaultValue(1000.0f).minimumValue(1.0f).maximumValue(10000.0f);
CONFIG(float, UnitTransparency).defaultValue(0.7f);
CONFIG(bool, UnitIconsAsUI).defaultValue(false).description("Draw unit icons like it is an UI element and not like unit's LOD.");
CONFIG(bool, UnitIconsHideWithUI).defaultValue(false).description("Hide unit icons when UI is hidden.");

CONFIG(int, MaxDynamicModelLights)
	.defaultValue(1)
	.minimumValue(0);

CONFIG(bool, AdvUnitShading).defaultValue(true).headlessValue(false).safemodeValue(false).description("Determines whether specular highlights and other lighting effects are rendered for units.");




CUnitDrawer* unitDrawer = nullptr;

static void LoadUnitExplosionGenerators() {
	using F = decltype(&UnitDef::AddModelExpGenID);
	using T = decltype(UnitDef::modelCEGTags);

	const auto LoadGenerators = [](UnitDef* ud, const F addExplGenID, const T& explGenTags, const char* explGenPrefix) {
		for (const auto& explGenTag: explGenTags) {
			if (explGenTag[0] == 0)
				break;

			// build a contiguous range of valid ID's
			(ud->*addExplGenID)(explGenHandler.LoadGeneratorID(explGenTag, explGenPrefix));
		}
	};

	for (unsigned int i = 0, n = unitDefHandler->NumUnitDefs(); i < n; i++) {
		UnitDef* ud = const_cast<UnitDef*>(unitDefHandler->GetUnitDefByID(i + 1));

		// piece- and crash-generators can only be custom so the prefix is not required to be given game-side
		LoadGenerators(ud, &UnitDef::AddModelExpGenID, ud->modelCEGTags,                "");
		LoadGenerators(ud, &UnitDef::AddPieceExpGenID, ud->pieceCEGTags, CEG_PREFIX_STRING);
		LoadGenerators(ud, &UnitDef::AddCrashExpGenID, ud->crashCEGTags, CEG_PREFIX_STRING);
	}
}


////

class CUnitDrawerHelper
{
public:
	virtual void BindOpaqueTex(const CS3OTextureHandler::S3OTexMat * textureMat) const = 0;
	virtual void UnbindOpaqueTex(const CS3OTextureHandler::S3OTexMat * textureMat) const = 0;
	virtual void BindShadowTex(const CS3OTextureHandler::S3OTexMat * textureMat) const = 0;
	virtual void UnbindShadowTex(const CS3OTextureHandler::S3OTexMat * textureMat)  const = 0;
	virtual void PushRenderState() const = 0;
	virtual void PopRenderState() const = 0;
public:
	static void EnableTexturesCommon() {
		glActiveTexture(GL_TEXTURE1);
		glEnable(GL_TEXTURE_2D);

		if (shadowHandler.ShadowsLoaded())
			shadowHandler.SetupShadowTexSampler(GL_TEXTURE2, true);

		glActiveTexture(GL_TEXTURE3);
		glEnable(GL_TEXTURE_CUBE_MAP);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapHandler.GetEnvReflectionTextureID());

		glActiveTexture(GL_TEXTURE4);
		glEnable(GL_TEXTURE_CUBE_MAP);
		glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapHandler.GetSpecularTextureID());

		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
	}

	static void DisableTexturesCommon() {
		glActiveTexture(GL_TEXTURE1);
		glDisable(GL_TEXTURE_2D);

		if (shadowHandler.ShadowsLoaded())
			shadowHandler.ResetShadowTexSampler(GL_TEXTURE2, true);

		glActiveTexture(GL_TEXTURE3);
		glDisable(GL_TEXTURE_CUBE_MAP);

		glActiveTexture(GL_TEXTURE4);
		glDisable(GL_TEXTURE_CUBE_MAP);

		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_TEXTURE_2D);
	}

	static void PushTransform(const CCamera* cam) {
		// set model-drawing transform; view is combined with projection
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glMultMatrixf(cam->GetViewMatrix());
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
	}

	static void PopTransform() {
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}

	static float4 GetTeamColor(int team, float alpha) {
		assert(teamHandler.IsValidTeam(team));

		const   CTeam* t = teamHandler.Team(team);
		const uint8_t* c = t->color;

		return (float4(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, alpha));
	}

public:
	template<typename T>
	static const CUnitDrawerHelper* GetInstance() {
		static const T instance;
		return &instance;
	}
public:
	static const std::array<const CUnitDrawerHelper*, MODELTYPE_CNT> unitDrawerHelpers;
};

class CUnitDrawerHelper3DO : public CUnitDrawerHelper
{
public:
	virtual void BindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {/*handled in PushRenderState()*/}
	virtual void UnbindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {/*handled in PopRenderState()*/}
	virtual void BindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, textureHandler3DO.GetAtlasTex2ID());
	}
	virtual void UnbindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
	}
	virtual void PushRenderState() const override {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, textureHandler3DO.GetAtlasTex2ID());
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureHandler3DO.GetAtlasTex1ID());

		glDisable(GL_CULL_FACE);
	}
	virtual void PopRenderState() const override {
		glEnable(GL_CULL_FACE);
	}
};

class CUnitDrawerHelperS3O : public CUnitDrawerHelper
{
public:
	virtual void BindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, textureMat->tex2);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureMat->tex1);
	}
	virtual void UnbindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	virtual void BindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, textureMat->tex2);
	}
	virtual void UnbindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
	}
	virtual void PushRenderState() const override {/* no need for primitve restart*/};
	virtual void PopRenderState() const override {/* no need for primitve restart*/};
};

class CUnitDrawerHelperASS : public CUnitDrawerHelper
{
public:
	virtual void BindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, textureMat->tex2);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureMat->tex1);
	}
	virtual void UnbindOpaqueTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	virtual void BindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, textureMat->tex2);
	}
	virtual void UnbindShadowTex(const CS3OTextureHandler::S3OTexMat* textureMat) const override {
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
	}
	virtual void PushRenderState() const override {};
	virtual void PopRenderState() const override {};
};


const std::array<const CUnitDrawerHelper*, MODELTYPE_CNT> CUnitDrawerHelper::unitDrawerHelpers = {
	CUnitDrawerHelper::GetInstance<CUnitDrawerHelper3DO>(),
	CUnitDrawerHelper::GetInstance<CUnitDrawerHelperS3O>(),
	CUnitDrawerHelper::GetInstance<CUnitDrawerHelperASS>(),
};
////


void CUnitDrawer::InitStatic()
{
	alphaValues.x = std::max(0.11f, std::min(1.0f, 1.0f - configHandler->GetFloat("UnitTransparency")));
	alphaValues.y = std::min(1.0f, alphaValues.x + 0.1f);
	alphaValues.z = std::min(1.0f, alphaValues.x + 0.2f);
	alphaValues.w = std::min(1.0f, alphaValues.x + 0.4f);

	LoadUnitExplosionGenerators();

	unitDrawers[UNIT_DRAWER_FFP ] = CUnitDrawer::GetInstance<CUnitDrawerFFP >();
	unitDrawers[UNIT_DRAWER_ARB ] = CUnitDrawer::GetInstance<CUnitDrawerARB >();
	unitDrawers[UNIT_DRAWER_GLSL] = CUnitDrawer::GetInstance<CUnitDrawerGLSL>();
	unitDrawers[UNIT_DRAWER_GL4 ] = CUnitDrawer::GetInstance<CUnitDrawerGL4 >();

	forceLegacyPath = false;
	wireFrameMode = false;

	drawForward = true;

	cubeMapInitialized = cubeMapHandler.Init();

	lightHandler.Init(2U, configHandler->GetInt("MaxDynamicModelLights"));

	// shared with FeatureDrawer!
	geomBuffer = LuaObjectDrawer::GetGeometryBuffer();

	SelectImplementation();
}

void CUnitDrawer::KillStatic(bool reload)
{
	for (auto& ud : unitDrawers) {
		ud = nullptr;
	}
	unitDrawer = nullptr;

	cubeMapHandler.Free();

	geomBuffer = nullptr;
}

void CUnitDrawer::SelectImplementation()
{
	if (!forceReselection)
		return;

	forceReselection = false;

	if (!advShading || !cubeMapInitialized) {
		SelectImplementation(UnitDrawerTypes::UNIT_DRAWER_FFP);
		return;
	}

	int best = UnitDrawerTypes::UNIT_DRAWER_FFP;
	for (int t = UnitDrawerTypes::UNIT_DRAWER_ARB; t < UnitDrawerTypes::UNIT_DRAWER_CNT; ++t) {
		if (forceLegacyPath && !unitDrawers[t]->IsLegacy())
			continue;

		if (unitDrawers[t]->CanEnable())
			best = t;
	}

	SelectImplementation(best);
}

void CUnitDrawer::SelectImplementation(int targetImplementation)
{
	selectedImplementation = targetImplementation;

	unitDrawer = unitDrawers[targetImplementation];
	assert(unitDrawer);
	assert(unitDrawer->CanEnable());
}

void CUnitDrawer::SunChangedStatic()
{
	for (auto ud : unitDrawers) {
		ud->SunChanged();
	}
}

void CUnitDrawer::BindModelTypeTexture(int mdlType, int texType)
{
	const auto texMat = textureHandlerS3O.GetTexture(texType);

	if (!shadowHandler.InShadowPass())
		CUnitDrawerHelper::unitDrawerHelpers[mdlType]->BindShadowTex(texMat);
	else
		CUnitDrawerHelper::unitDrawerHelpers[mdlType]->BindOpaqueTex(texMat);
}

void CUnitDrawer::PushModelRenderState(int mdlType)
{
	assert(CUnitDrawerHelper::unitDrawerHelpers[mdlType]);
	CUnitDrawerHelper::unitDrawerHelpers[mdlType]->PushRenderState();
}

void CUnitDrawer::PushModelRenderState(const S3DModel* m)
{
	PushModelRenderState(m->type);
	BindModelTypeTexture(m->type, m->textureType);
}

void CUnitDrawer::PushModelRenderState(const CSolidObject* o) { PushModelRenderState(o->model); }

void CUnitDrawer::PopModelRenderState(int mdlType)
{
	assert(CUnitDrawerHelper::unitDrawerHelpers[mdlType]);
	CUnitDrawerHelper::unitDrawerHelpers[mdlType]->PopRenderState();
}

void CUnitDrawer::PopModelRenderState(const S3DModel* m) { PopModelRenderState(m->type); }
void CUnitDrawer::PopModelRenderState(const CSolidObject* o) { PopModelRenderState(o->model); }

bool CUnitDrawer::ObjectVisibleReflection(const float3 objPos, const float3 camPos, float maxRadius)
{
	if (objPos.y < 0.0f)
		return (CGround::GetApproximateHeight(objPos.x, objPos.z, false) <= maxRadius);

	const float dif = objPos.y - camPos.y;

	float3 zeroPos;
	zeroPos += (camPos * (objPos.y / dif));
	zeroPos += (objPos * (-camPos.y / dif));

	return (CGround::GetApproximateHeight(zeroPos.x, zeroPos.z, false) <= maxRadius);
}

///

void CUnitDrawerLegacy::SetupOpaqueDrawing(bool deferredPass)
{
	glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE * wireFrameMode + GL_FILL * (1 - wireFrameMode));

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	glAlphaFunc(GL_GREATER, 0.5f);
	glEnable(GL_ALPHA_TEST);

	Enable(deferredPass, false);
}

void CUnitDrawerLegacy::ResetOpaqueDrawing(bool deferredPass)
{
	Disable(deferredPass);
	glPopAttrib();
}

void CUnitDrawerLegacy::SetupAlphaDrawing(bool deferredPass)
{
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE * wireFrameMode + GL_FILL * (1 - wireFrameMode));

	Enable(/*deferredPass*/ false, true);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glDepthMask(GL_FALSE);
}

void CUnitDrawerLegacy::ResetAlphaDrawing(bool deferredPass)
{
	Disable(/*deferredPass*/ false);
	glPopAttrib();
}


bool CUnitDrawerLegacy::SetTeamColour(int team, const float2 alpha) const
{
	// need this because we can be called by no-team projectiles
	if (!teamHandler.IsValidTeam(team))
		return false;

	// should be an assert, but projectiles (+FlyingPiece) would trigger it
	if (shadowHandler.InShadowPass())
		return false;
}

void CUnitDrawerLegacy::DrawUnitModel(const CUnit* unit, bool noLuaCall)
{
	if (!noLuaCall && unit->luaDraw && eventHandler.DrawUnit(unit))
		return;

	unit->localModel.Draw();
}

bool CUnitDrawerFFP::SetTeamColour(int team, const float2 alpha) const
{
	if (!CUnitDrawerLegacy::SetTeamColour(team, alpha))
		return false;

	// non-shader case via texture combiners
	const float4 m = { 1.0f, 1.0f, 1.0f, alpha.x };

	glActiveTexture(GL_TEXTURE0);
	glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, std::move(CUnitDrawerHelper::GetTeamColor(team, alpha.x)));
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &m.x);

	return true;
}