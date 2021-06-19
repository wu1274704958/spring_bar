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
#include "Rendering/Env/SunLighting.h"
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

	static inline float GetUnitIconScale(const CUnit* unit) {
		float scale = unit->myIcon->GetSize();

		if (!minimap->UseUnitIcons())
			return scale;
		if (!unit->myIcon->GetRadiusAdjust())
			return scale;

		const unsigned short losStatus = unit->losStatus[gu->myAllyTeam];
		const unsigned short prevMask = (LOS_PREVLOS | LOS_CONTRADAR);
		const bool unitVisible = ((losStatus & LOS_INLOS) || ((losStatus & LOS_INRADAR) && ((losStatus & prevMask) == prevMask)));

		if ((unitVisible || gu->spectatingFullView)) {
			scale *= (unit->radius / unit->myIcon->GetRadiusScale());
		}

		return scale;
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
	LuaObjectDrawer::ReadLODScales(LUAOBJ_UNIT);

	alphaValues.x = std::max(0.11f, std::min(1.0f, 1.0f - configHandler->GetFloat("UnitTransparency")));
	alphaValues.y = std::min(1.0f, alphaValues.x + 0.1f);
	alphaValues.z = std::min(1.0f, alphaValues.x + 0.2f);
	alphaValues.w = std::min(1.0f, alphaValues.x + 0.4f);

	LoadUnitExplosionGenerators();

	CUnitDrawer::InitInstance<CUnitDrawerFFP >(UNIT_DRAWER_FFP);
	//CUnitDrawer::GetInstance<CUnitDrawerARB >(UNIT_DRAWER_ARB);
	//CUnitDrawer::GetInstance<CUnitDrawerGLSL>(UNIT_DRAWER_GLSL);
	//CUnitDrawer::GetInstance<CUnitDrawerGL4 >(UNIT_DRAWER_GL4);

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
	for (int t = UNIT_DRAWER_FFP; t < UNIT_DRAWER_CNT; ++t) {
		CUnitDrawer::KillInstance(t);
	}

	unitDrawer = nullptr;

	cubeMapHandler.Free();

	geomBuffer = nullptr;
}

void CUnitDrawer::ForceLegacyPath()
{
	reselectionRequested = true;
	forceLegacyPath = true;
	LOG_L(L_WARNING, "[CUnitDrawer]: Using legacy (slow) unit renderer! This is caused by insufficient GPU/driver capabilities or by use of old Lua rendering API");
}

void CUnitDrawer::SelectImplementation(bool forceReselection)
{
	if (!reselectionRequested && !forceReselection)
		return;

	reselectionRequested = false;

	if (!advShading || !cubeMapInitialized) {
		SelectImplementation(UnitDrawerTypes::UNIT_DRAWER_FFP);
		return;
	}

	int best = UnitDrawerTypes::UNIT_DRAWER_FFP;
	for (int t = UnitDrawerTypes::UNIT_DRAWER_ARB; t < UnitDrawerTypes::UNIT_DRAWER_CNT; ++t) {
		if (unitDrawers[t] == nullptr)
			continue;

		if (forceLegacyPath && !unitDrawers[t]->IsLegacy())
			continue;

		if (unitDrawers[t]->CanEnable())
			best = t;
	}

	SelectImplementation(best);
}

void CUnitDrawer::SelectImplementation(int targetImplementation)
{
	//selectedImplementation = targetImplementation;

	unitDrawer = unitDrawers[targetImplementation];
	assert(unitDrawer);
	assert(unitDrawer->CanEnable());
}

void CUnitDrawer::Update()
{
	SelectImplementation();
	unitDrawer->unitDrawerData.Update();
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

	if (shadowHandler.InShadowPass())
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

void CUnitDrawer::PopModelRenderState(const S3DModel*     m) { PopModelRenderState(m->type); }
void CUnitDrawer::PopModelRenderState(const CSolidObject* o) { PopModelRenderState(o->model); }

bool CUnitDrawer::ObjectVisibleReflection(const float3 objPos, const float3 camPos, float maxRadius)
{
	if (objPos.y < 0.0f)
		return (CGround::GetApproximateHeight(objPos.x, objPos.z, false) <= maxRadius);

	const float dif = objPos.y - camPos.y;

	float3 zeroPos;
	zeroPos += (camPos * ( objPos.y / dif));
	zeroPos += (objPos * (-camPos.y / dif));

	return (CGround::GetApproximateHeight(zeroPos.x, zeroPos.z, false) <= maxRadius);
}

///

void CUnitDrawerLegacy::SetupOpaqueDrawing(bool deferredPass) const
{
	glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE * wireFrameMode + GL_FILL * (1 - wireFrameMode));

	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

	glAlphaFunc(GL_GREATER, 0.5f);
	glEnable(GL_ALPHA_TEST);

	Enable(deferredPass, false);
}

void CUnitDrawerLegacy::ResetOpaqueDrawing(bool deferredPass) const
{
	Disable(deferredPass);
	glPopAttrib();
}

void CUnitDrawerLegacy::SetupAlphaDrawing(bool deferredPass) const
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

void CUnitDrawerLegacy::ResetAlphaDrawing(bool deferredPass) const
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

	return true;
}

void CUnitDrawerLegacy::DrawUnitModel(const CUnit* unit, bool noLuaCall) const
{
	if (!noLuaCall && unit->luaDraw && eventHandler.DrawUnit(unit))
		return;

	unit->localModel.Draw();
}

void CUnitDrawerLegacy::DrawUnitNoTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) const
{
	//if (!CheckLegacyDrawing(unit, preList, postList, lodCall, noLuaCall))
		//return;

	const bool noNanoDraw = lodCall || !unit->beingBuilt || !unit->unitDef->showNanoFrame;
	const bool shadowPass = shadowHandler.InShadowPass();

	if (preList != 0) {
		glCallList(preList);
	}

	// if called from LuaObjectDrawer, unit has a custom material
	//
	// we want Lua-material shaders to have full control over build
	// visualisation, so keep it simple and make LOD-calls draw the
	// full model
	//
	// NOTE: "raw" calls will no longer skip DrawUnitBeingBuilt
	//

	//drawModelFuncs[std::max(noNanoDraw * 2, shadowPass)](unit, noLuaCall);
	if (noNanoDraw)
		DrawUnitModel(unit, noLuaCall);
	else {
		if (shadowPass)
			DrawUnitModelBeingBuiltShadow(unit, noLuaCall);
		else
			DrawUnitModelBeingBuiltOpaque(unit, noLuaCall);
	}


	if (postList != 0) {
		glCallList(postList);
	}
}

void CUnitDrawerLegacy::DrawUnitTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) const
{
	glPushMatrix();
	glMultMatrixf(unit->GetTransformMatrix());

	DrawUnitNoTrans(unit, preList, postList, lodCall, noLuaCall);

	glPopMatrix();
}

void CUnitDrawerLegacy::Draw(bool drawReflection, bool drawRefraction) const
{
	sky->SetupFog();

	assert((CCameraHandler::GetActiveCamera())->GetCamType() != CCamera::CAMTYPE_SHADOW);

	// first do the deferred pass; conditional because
	// most of the water renderers use their own FBO's
	if (drawDeferred && !drawReflection && !drawRefraction)
		LuaObjectDrawer::DrawDeferredPass(LUAOBJ_UNIT);

	// now do the regular forward pass
	if (drawForward)
		DrawOpaquePass(false, drawReflection, drawRefraction);

	farTextureHandler->Draw();

	glDisable(GL_FOG);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_TEXTURE_2D);
}

void CUnitDrawerLegacy::DrawOpaquePass(bool deferredPass, bool drawReflection, bool drawRefraction) const
{
	SetupOpaqueDrawing(deferredPass);

	for (int modelType = MODELTYPE_3DO; modelType < MODELTYPE_CNT; modelType++) {
		if (unitDrawerData.GetOpaqueModelRenderer(modelType).GetNumObjects() == 0)
			continue;

		PushModelRenderState(modelType);
		DrawOpaqueUnits(modelType, drawReflection, drawRefraction);
		DrawOpaqueAIUnits(modelType);
		PopModelRenderState(modelType);
	}

	ResetOpaqueDrawing(deferredPass);

	// draw all custom'ed units that were bypassed in the loop above
	LuaObjectDrawer::SetDrawPassGlobalLODFactor(LUAOBJ_UNIT);
	LuaObjectDrawer::DrawOpaqueMaterialObjects(LUAOBJ_UNIT, deferredPass);
}

void CUnitDrawerLegacy::DrawShadowPass() const
{
	glColor3f(1.0f, 1.0f, 1.0f);
	glPolygonOffset(1.0f, 1.0f);
	glEnable(GL_POLYGON_OFFSET_FILL);

	#ifdef UNIT_SHADOW_ALPHA_MASKING
		glAlphaFunc(GL_GREATER, 0.5f);
		glEnable(GL_ALPHA_TEST);
	#endif

	Shader::IProgramObject* po = shadowHandler.GetShadowGenProg(CShadowHandler::SHADOWGEN_PROGRAM_MODEL);
	po->Enable();

	{
		assert((CCameraHandler::GetActiveCamera())->GetCamType() == CCamera::CAMTYPE_SHADOW);

		// 3DO's have clockwise-wound faces and
		// (usually) holes, so disable backface
		// culling for them
		if (unitDrawerData.GetOpaqueModelRenderer(MODELTYPE_3DO).GetNumObjects() > 0) {
			glDisable(GL_CULL_FACE);
			DrawOpaqueUnitsShadow(MODELTYPE_3DO);
			glEnable(GL_CULL_FACE);
		}

		for (int modelType = MODELTYPE_S3O; modelType < MODELTYPE_CNT; modelType++) {
			if (unitDrawerData.GetOpaqueModelRenderer(modelType).GetNumObjects() == 0)
				continue;
			// note: just use DrawOpaqueUnits()? would
			// save texture switches needed anyway for
			// UNIT_SHADOW_ALPHA_MASKING
			DrawOpaqueUnitsShadow(modelType);
		}
	}

	po->Disable();

	#ifdef UNIT_SHADOW_ALPHA_MASKING
		glDisable(GL_ALPHA_TEST);
	#endif

	glDisable(GL_POLYGON_OFFSET_FILL);

	LuaObjectDrawer::SetDrawPassGlobalLODFactor(LUAOBJ_UNIT);
	LuaObjectDrawer::DrawShadowMaterialObjects(LUAOBJ_UNIT, false);
}

void CUnitDrawerLegacy::DrawAlphaPass() const
{
	{
		SetupAlphaDrawing(false);

		if (UseAdvShading())
			glDisable(GL_ALPHA_TEST);

		for (int modelType = MODELTYPE_3DO; modelType < MODELTYPE_CNT; modelType++) {
			if (unitDrawerData.GetAlphaModelRenderer(modelType).GetNumObjects() == 0)
				continue;

			PushModelRenderState(modelType);
			DrawAlphaUnits(modelType);
			DrawAlphaAIUnits(modelType);
			PopModelRenderState(modelType);
		}

		if (UseAdvShading())
			glEnable(GL_ALPHA_TEST);

		ResetAlphaDrawing(false);
	}

	LuaObjectDrawer::SetDrawPassGlobalLODFactor(LUAOBJ_UNIT);
	LuaObjectDrawer::DrawAlphaMaterialObjects(LUAOBJ_UNIT, false);
}

void CUnitDrawerLegacy::DrawUnitMiniMapIcons() const
{
	CVertexArray* va = GetVertexArray();

	for (const auto& [icon, units] : unitDrawerData.GetUnitsByIcon()) {

		if (icon == nullptr)
			continue;
		if (units.empty())
			continue;

		va->Initialize();
		va->EnlargeArrays(units.size() * 4, 0, VA_SIZE_2DTC);
		icon->BindTexture();

		for (const CUnit* unit : units) {
			assert(unit->myIcon == icon);
			DrawUnitMiniMapIcon(unit, va);
		}

		va->DrawArray2dTC(GL_QUADS);
	}
}

void CUnitDrawerLegacy::DrawUnitIcons() const
{
	// draw unit icons and radar blips
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.05f);

	// A2C effectiveness is limited below four samples
	if (globalRendering->msaaLevel >= 4)
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE_ARB);

	for (CUnit* u : unitDrawerData.GetIconUnits()) {
		const unsigned short closBits = (u->losStatus[gu->myAllyTeam] & (LOS_INLOS));
		const unsigned short plosBits = (u->losStatus[gu->myAllyTeam] & (LOS_PREVLOS | LOS_CONTRADAR));

		DrawIcon(u, !gu->spectatingFullView && closBits == 0 && plosBits != (LOS_PREVLOS | LOS_CONTRADAR));
	}

	glPopAttrib();
}

void CUnitDrawerLegacy::DrawUnitIconsScreen() const
{
	if (game->hideInterface && unitDrawerData.iconHideWithUI)
		return;

	// draw unit icons and radar blips
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.05f);

	CVertexArray* va = GetVertexArray();

	for (const auto& [icon, units] : unitDrawerData.GetUnitsByIcon())
	{

		if (icon == nullptr)
			continue;
		if (units.empty())
			continue;

		va->Initialize();
		va->EnlargeArrays(units.size() * 4, 0, VA_SIZE_2DTC);
		icon->BindTexture();

		for (const CUnit* unit : units)
		{
			if (unit->noDraw)
				continue;
			if (unit->IsInVoid())
				continue;
			if (unit->health <= 0 || unit->beingBuilt)
				continue;

			const unsigned short closBits = (unit->losStatus[gu->myAllyTeam] & (LOS_INLOS));
			const unsigned short plosBits = (unit->losStatus[gu->myAllyTeam] & (LOS_PREVLOS | LOS_CONTRADAR));

			assert(unit->myIcon == icon);
			DrawIconScreenArray(unit, icon, !gu->spectatingFullView && closBits == 0 && plosBits != (LOS_PREVLOS | LOS_CONTRADAR), unitDrawerData.iconZoomDist, va);
		}

		va->DrawArray2dTC(GL_QUADS);
	}
	glPopAttrib();
}

void CUnitDrawerLegacy::DrawOpaqueUnitsShadow(int modelType) const
{
	const auto& mdlRenderer = unitDrawerData.GetOpaqueModelRenderer(modelType);
	// const auto& unitBinKeys = mdlRenderer.GetObjectBinKeys();

	for (unsigned int i = 0, n = mdlRenderer.GetNumObjectBins(); i < n; i++) {
		// only need to bind the atlas once for 3DO's, but KISS
		assert((modelType != MODELTYPE_3DO) || (mdlRenderer.GetObjectBinKey(i) == 0));

		//shadowTexBindFuncs[modelType](textureHandlerS3O.GetTexture(mdlRenderer.GetObjectBinKey(i)));
		const auto* texMat = textureHandlerS3O.GetTexture(mdlRenderer.GetObjectBinKey(i));
		CUnitDrawerHelper::unitDrawerHelpers[modelType]->BindShadowTex(texMat);

		for (CUnit* unit : mdlRenderer.GetObjectBin(i)) {
			DrawOpaqueUnitShadow(unit);
		}

		//shadowTexKillFuncs[modelType](nullptr);
		CUnitDrawerHelper::unitDrawerHelpers[modelType]->UnbindShadowTex(nullptr);
	}
}

void CUnitDrawerLegacy::DrawOpaqueUnits(int modelType, bool drawReflection, bool drawRefraction) const
{
	const auto& mdlRenderer = unitDrawerData.GetOpaqueModelRenderer(modelType);
	// const auto& unitBinKeys = mdlRenderer.GetObjectBinKeys();

	for (unsigned int i = 0, n = mdlRenderer.GetNumObjectBins(); i < n; i++) {
		BindModelTypeTexture(modelType, mdlRenderer.GetObjectBinKey(i));

		for (CUnit* unit : mdlRenderer.GetObjectBin(i)) {
			DrawOpaqueUnit(unit, drawReflection, drawRefraction);
		}
	}
}

void CUnitDrawerLegacy::DrawAlphaUnits(int modelType) const
{
	{
		const auto& mdlRenderer = unitDrawerData.GetAlphaModelRenderer(modelType);
		// const auto& unitBinKeys = mdlRenderer.GetObjectBinKeys();

		for (unsigned int i = 0, n = mdlRenderer.GetNumObjectBins(); i < n; i++) {
			BindModelTypeTexture(modelType, mdlRenderer.GetObjectBinKey(i));

			for (CUnit* unit : mdlRenderer.GetObjectBin(i)) {
				DrawAlphaUnit(unit, modelType, false);
			}
		}
	}

	// living and dead ghosted buildings
	if (!gu->spectatingFullView)
		DrawGhostedBuildings(modelType);
}

void CUnitDrawerLegacy::DrawOpaqueAIUnits(int modelType) const
{
	const std::vector<CUnitDrawerData::TempDrawUnit>& tmpOpaqueUnits = unitDrawerData.GetTempOpaqueDrawUnits(modelType);

	// NOTE: not type-sorted
	for (const auto& unit : tmpOpaqueUnits) {
		if (!camera->InView(unit.pos, 100.0f))
			continue;

		DrawOpaqueAIUnit(unit);
	}
}

void CUnitDrawerLegacy::DrawAlphaAIUnits(int modelType) const
{
	const std::vector<CUnitDrawerData::TempDrawUnit>& tmpAlphaUnits = unitDrawerData.GetTempAlphaDrawUnits(modelType);

	// NOTE: not type-sorted
	for (const auto& unit : tmpAlphaUnits) {
		if (!camera->InView(unit.pos, 100.0f))
			continue;

		DrawAlphaAIUnit(unit);
		DrawAlphaAIUnitBorder(unit);
	}
}

void CUnitDrawerLegacy::DrawGhostedBuildings(int modelType) const
{
	const auto& deadGhostedBuildings = unitDrawerData.GetDeadGhostBuildings(gu->myAllyTeam, modelType);
	const auto& liveGhostedBuildings = unitDrawerData.GetLiveGhostBuildings(gu->myAllyTeam, modelType);

	glColor4f(0.6f, 0.6f, 0.6f, alphaValues.y);

	// buildings that died while ghosted
	for (GhostSolidObject* dgb : deadGhostedBuildings) {
		if (camera->InView(dgb->pos, dgb->model->GetDrawRadius())) {
			glPushMatrix();
			glTranslatef3(dgb->pos);
			glRotatef(dgb->facing * 90.0f, 0, 1, 0);

			BindModelTypeTexture(modelType, dgb->model->textureType);
			SetTeamColour(dgb->team, float2(alphaValues.y, 1.0f));

			dgb->model->DrawStatic();
			glPopMatrix();
			dgb->lastDrawFrame = globalRendering->drawFrame;
		}
	}

	for (CUnit* lgb : liveGhostedBuildings) {
		DrawAlphaUnit(lgb, modelType, true);
	}
}

void CUnitDrawerLegacy::DrawOpaqueUnit(CUnit* unit, bool drawReflection, bool drawRefraction) const
{
	if (!CanDrawOpaqueUnit(unit, drawReflection, drawRefraction))
		return;

	if ((unit->pos).SqDistance(camera->GetPos()) > (unit->sqRadius * unitDrawerData.unitDrawDistSqr)) {
		farTextureHandler->Queue(unit);
		return;
	}

	if (LuaObjectDrawer::AddOpaqueMaterialObject(unit, LUAOBJ_UNIT))
		return;

	// draw the unit with the default (non-Lua) material
	SetTeamColour(unit->team);
	DrawUnitTrans(unit, 0, 0, false, false);
}

void CUnitDrawerLegacy::DrawOpaqueUnitShadow(CUnit* unit) const
{
	if (!CanDrawOpaqueUnitShadow(unit))
		return;

	if (LuaObjectDrawer::AddShadowMaterialObject(unit, LUAOBJ_UNIT))
		return;

	DrawUnitTrans(unit, 0, 0, false, false);
}

void CUnitDrawerLegacy::DrawAlphaUnit(CUnit* unit, int modelType, bool drawGhostBuildingsPass) const
{
	if (!camera->InView(unit->drawMidPos, unit->GetDrawRadius()))
		return;

	if (LuaObjectDrawer::AddAlphaMaterialObject(unit, LUAOBJ_UNIT))
		return;

	const unsigned short losStatus = unit->losStatus[gu->myAllyTeam];

	if (drawGhostBuildingsPass) {
		// check for decoy models
		const UnitDef* decoyDef = unit->unitDef->decoyDef;
		const S3DModel* model = nullptr;

		if (decoyDef == nullptr) {
			model = unit->model;
		}
		else {
			model = decoyDef->LoadModel();
		}

		// FIXME: needs a second pass
		if (model->type != modelType)
			return;

		// ghosted enemy units
		if (losStatus & LOS_CONTRADAR) {
			glColor4f(0.9f, 0.9f, 0.9f, alphaValues.z);
		}
		else {
			glColor4f(0.6f, 0.6f, 0.6f, alphaValues.y);
		}

		glPushMatrix();
		glTranslatef3(unit->drawPos);
		glRotatef(unit->buildFacing * 90.0f, 0, 1, 0);

		// the units in liveGhostedBuildings[modelType] are not
		// sorted by textureType, but we cannot merge them with
		// alphaModelRenderers[modelType] either since they are
		// not actually cloaked
		BindModelTypeTexture(modelType, model->textureType);

		SetTeamColour(unit->team, float2((losStatus & LOS_CONTRADAR) ? alphaValues.z : alphaValues.y, 1.0f));
		model->DrawStatic();
		glPopMatrix();

		glColor4f(1.0f, 1.0f, 1.0f, alphaValues.x);
		return;
	}

	if (unit->isIcon)
		return;

	if ((losStatus & LOS_INLOS) || gu->spectatingFullView) {
		SetTeamColour(unit->team, float2(alphaValues.x, 1.0f));
		DrawUnitTrans(unit, 0, 0, false, false);
	}
}

void CUnitDrawerLegacy::DrawOpaqueAIUnit(const CUnitDrawerData::TempDrawUnit& unit) const
{
	glPushMatrix();
	glTranslatef3(unit.pos);
	glRotatef(unit.rotation * math::RAD_TO_DEG, 0.0f, 1.0f, 0.0f);

	const UnitDef* def = unit.unitDef;
	const S3DModel* mdl = def->model;

	assert(mdl != nullptr);

	BindModelTypeTexture(mdl->type, mdl->textureType);
	SetTeamColour(unit.team);
	mdl->DrawStatic();

	glPopMatrix();
}

void CUnitDrawerLegacy::DrawAlphaAIUnit(const CUnitDrawerData::TempDrawUnit& unit) const
{
	glPushMatrix();
	glTranslatef3(unit.pos);
	glRotatef(unit.rotation * math::RAD_TO_DEG, 0.0f, 1.0f, 0.0f);

	const UnitDef* def = unit.unitDef;
	const S3DModel* mdl = def->model;

	assert(mdl != nullptr);

	BindModelTypeTexture(mdl->type, mdl->textureType);
	SetTeamColour(unit.team, float2(alphaValues.x, 1.0f));
	mdl->DrawStatic();

	glPopMatrix();
}

void CUnitDrawerLegacy::DrawAlphaAIUnitBorder(const CUnitDrawerData::TempDrawUnit& unit) const
{
	if (!unit.drawBorder)
		return;

	SetTeamColour(unit.team, float2(alphaValues.w, 1.0f));

	const BuildInfo buildInfo(unit.unitDef, unit.pos, unit.facing);
	const float3 buildPos = CGameHelper::Pos2BuildPos(buildInfo, false);

	const float xsize = buildInfo.GetXSize() * (SQUARE_SIZE >> 1);
	const float zsize = buildInfo.GetZSize() * (SQUARE_SIZE >> 1);

	glColor4f(0.2f, 1, 0.2f, alphaValues.w);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_LINE_STRIP);
	glVertexf3(buildPos + float3(xsize, 1.0f, zsize));
	glVertexf3(buildPos + float3(-xsize, 1.0f, zsize));
	glVertexf3(buildPos + float3(-xsize, 1.0f, -zsize));
	glVertexf3(buildPos + float3(xsize, 1.0f, -zsize));
	glVertexf3(buildPos + float3(xsize, 1.0f, zsize));
	glEnd();
	glColor4f(1.0f, 1.0f, 1.0f, alphaValues.x);
	glEnable(GL_TEXTURE_2D);
}

void CUnitDrawerLegacy::DrawUnitMiniMapIcon(const CUnit* unit, CVertexArray* va) const
{
	if (unit->noMinimap)
		return;
	if (unit->myIcon == nullptr)
		return;
	if (unit->IsInVoid())
		return;

	const unsigned char defaultColor[4] = { 255, 255, 255, 255 };
	const unsigned char* color = &defaultColor[0];

	if (!unit->isSelected) {
		if (minimap->UseSimpleColors()) {
			if (unit->team == gu->myTeam) {
				color = minimap->GetMyTeamIconColor();
			}
			else if (teamHandler.Ally(gu->myAllyTeam, unit->allyteam)) {
				color = minimap->GetAllyTeamIconColor();
			}
			else {
				color = minimap->GetEnemyTeamIconColor();
			}
		}
		else {
			color = teamHandler.Team(unit->team)->color;
		}
	}

	const float iconScale = CUnitDrawerHelper::GetUnitIconScale(unit);
	const float3& iconPos = (!gu->spectatingFullView) ?
		unit->GetObjDrawErrorPos(gu->myAllyTeam) :
		unit->GetObjDrawMidPos();

	const float iconSizeX = (iconScale * minimap->GetUnitSizeX());
	const float iconSizeY = (iconScale * minimap->GetUnitSizeY());

	const float x0 = iconPos.x - iconSizeX;
	const float x1 = iconPos.x + iconSizeX;
	const float y0 = iconPos.z - iconSizeY;
	const float y1 = iconPos.z + iconSizeY;

	unit->myIcon->DrawArray(va, x0, y0, x1, y1, color);
}

void CUnitDrawerLegacy::DrawIcon(CUnit* unit, bool useDefaultIcon)
{
	// iconUnits should not never contain void-space units, see UpdateUnitIconState
	assert(!unit->IsInVoid());

	// If the icon is to be drawn as a radar blip, we want to get the default icon.
	const icon::CIconData* iconData = nullptr;

	if (useDefaultIcon) {
		iconData = icon::iconHandler.GetDefaultIconData();
	}
	else {
		iconData = unit->unitDef->iconType.GetIconData();
	}

	// drawMidPos is auto-calculated now; can wobble on its own as pieces move
	float3 pos = (!gu->spectatingFullView) ?
		unit->GetObjDrawErrorPos(gu->myAllyTeam) :
		unit->GetObjDrawMidPos();

	// make sure icon is above ground (needed before we calculate scale below)
	const float h = CGround::GetHeightReal(pos.x, pos.z, false);

	pos.y = std::max(pos.y, h);

	// Calculate the icon size. It scales with:
	//  * The square root of the camera distance.
	//  * The mod defined 'iconSize' (which acts a multiplier).
	//  * The unit radius, depending on whether the mod defined 'radiusadjust' is true or false.
	const float dist = std::min(8000.0f, fastmath::sqrt_builtin(camera->GetPos().SqDistance(pos)));
	const float iconScaleDist = 0.4f * fastmath::sqrt_builtin(dist); // makes far icons bigger
	float scale = iconData->GetSize() * iconScaleDist;

	if (iconData->GetRadiusAdjust() && !useDefaultIcon)
		scale *= (unit->radius / iconData->GetRadiusScale());

	// make sure icon is not partly under ground
	pos.y = std::max(pos.y, h + (unit->iconRadius = scale));

	// use white for selected units
	const uint8_t* colors[] = { teamHandler.Team(unit->team)->color, color4::white };
	const uint8_t* color = colors[unit->isSelected];

	glColor3ubv(color);

	// calculate the vertices
	const float3 dy = camera->GetUp() * scale;
	const float3 dx = camera->GetRight() * scale;
	const float3 vn = pos - dx;
	const float3 vp = pos + dx;
	const float3 vnn = vn - dy;
	const float3 vpn = vp - dy;
	const float3 vnp = vn + dy;
	const float3 vpp = vp + dy;

	// Draw the icon.
	iconData->Draw(vnn, vpn, vnp, vpp);
}

void CUnitDrawerLegacy::DrawIconScreenArray(const CUnit* unit, const icon::CIconData* icon, bool useDefaultIcon, const float dist, CVertexArray* va) const
{
	// iconUnits should not never contain void-space units, see UpdateUnitIconState
	assert(!unit->IsInVoid());

	// drawMidPos is auto-calculated now; can wobble on its own as pieces move
	float3 pos = (!gu->spectatingFullView) ?
		unit->GetObjDrawErrorPos(gu->myAllyTeam) :
		unit->GetObjDrawMidPos();

	pos = camera->CalcWindowCoordinates(pos);
	if (pos.z < 0)
		return;

	// use white for selected units
	const uint8_t* srcColor = unit->isSelected ? color4::white : teamHandler.Team(unit->team)->color;
	uint8_t color[4] = { srcColor[0], srcColor[1], srcColor[2], 255 };

	float unitRadiusMult = icon->GetSize();
	if (icon->GetRadiusAdjust() && !useDefaultIcon)
		unitRadiusMult *= (unit->radius / icon->GetRadiusScale());
	unitRadiusMult = (unitRadiusMult - 1) * 0.75 + 1;

	// fade icons away in high zoom in levels
	if (!unit->isIcon)
		if (dist / unitRadiusMult < unitDrawerData.iconFadeVanish)
			return;
		else if (unitDrawerData.iconFadeVanish < unitDrawerData.iconFadeStart && dist / unitRadiusMult < unitDrawerData.iconFadeStart)
			// alpha range [64, 255], since icons is unrecognisable with alpha < 64
			color[3] = 64 + 191.0f * (dist / unitRadiusMult - unitDrawerData.iconFadeVanish) / (unitDrawerData.iconFadeStart - unitDrawerData.iconFadeVanish);

	// calculate the vertices
	const float offset = unitDrawerData.iconSizeBase / 2.0f * unitRadiusMult;

	const float x0 = (pos.x - offset) / globalRendering->viewSizeX;
	const float y0 = (pos.y + offset) / globalRendering->viewSizeY;
	const float x1 = (pos.x + offset) / globalRendering->viewSizeX;
	const float y1 = (pos.y - offset) / globalRendering->viewSizeY;

	if (x1 < 0 || x0 > 1 || y0 < 0 || y1 > 1)
		return; // don't try to draw outside the screen

	// Draw the icon.
	icon->DrawArray(va, x0, y0, x1, y1, color);
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

void CUnitDrawerFFP::Enable(bool deferredPass, bool alphaPass) const
{
	glEnable(GL_LIGHTING);
	// only for the advshading=0 case
	glLightfv(GL_LIGHT1, GL_POSITION, sky->GetLight()->GetLightDir());
	glLightfv(GL_LIGHT1, GL_AMBIENT, sunLighting->modelAmbientColor);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, sunLighting->modelDiffuseColor);
	glLightfv(GL_LIGHT1, GL_SPECULAR, sunLighting->modelSpecularColor);
	glEnable(GL_LIGHT1);

	CUnitDrawerFFP::SetupBasicS3OTexture1();
	CUnitDrawerFFP::SetupBasicS3OTexture0();

	const float4 color = { 1.0f, 1.0f, 1.0, mix(1.0f, alphaValues.x, (1.0f * alphaPass)) };

	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &color.x);
	glColor4fv(&color.x);

	CUnitDrawerHelper::PushTransform(camera);
}

void CUnitDrawerFFP::Disable(bool deferredPass) const
{
	CUnitDrawerHelper::PopTransform();

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glDisable(GL_LIGHTING);
	glDisable(GL_LIGHT1);

	CUnitDrawerFFP::CleanupBasicS3OTexture1();
	CUnitDrawerFFP::CleanupBasicS3OTexture0();
}


/**
 * Set up the texture environment in texture unit 0
 * to give an S3O texture its team-colour.
 *
 * Also:
 * - call SetBasicTeamColour to set the team colour to transform to.
 * - Replace the output alpha channel. If not, only the team-coloured bits will show, if that. Or something.
 */
void CUnitDrawerFFP::SetupBasicS3OTexture0()
{
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);

	// RGB = Texture * (1 - Alpha) + Teamcolor * Alpha
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);

	// ALPHA = Ignore
}

/**
 * This sets the first texture unit to GL_MODULATE the colours from the
 * first texture unit with the current glColor.
 *
 * Normal S3O drawing sets the color to full white; translucencies
 * use this setup to 'tint' the drawn model.
 *
 * - Leaves glActivateTextureARB at the first unit.
 * - This doesn't tinker with the output alpha, either.
 */
void CUnitDrawerFFP::SetupBasicS3OTexture1()
{
	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);

	// RGB = Primary Color * Previous
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PRIMARY_COLOR_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB);

	// ALPHA = Current alpha * Alpha mask
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PRIMARY_COLOR_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA);
}

void CUnitDrawerFFP::CleanupBasicS3OTexture1()
{
	// reset texture1 state
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void CUnitDrawerFFP::CleanupBasicS3OTexture0()
{
	// reset texture0 state
	glActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PREVIOUS_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_CONSTANT_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_SRC_ALPHA);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

bool CUnitDrawer::CanDrawOpaqueUnit(
	const CUnit* unit,
	bool drawReflection,
	bool drawRefraction
) const {
	if (unit == (drawReflection ? nullptr : (gu->GetMyPlayer())->fpsController.GetControllee()))
		return false;
	if (unit->noDraw)
		return false;
	if (unit->IsInVoid())
		return false;
	// unit will be drawn as icon instead
	if (unit->isIcon)
		return false;

	if (!(unit->losStatus[gu->myAllyTeam] & LOS_INLOS) && !gu->spectatingFullView)
		return false;

	// either PLAYER or UWREFL
	const CCamera* cam = CCameraHandler::GetActiveCamera();

	if (drawRefraction && !unit->IsInWater())
		return false;

	if (drawReflection && !ObjectVisibleReflection(unit->drawMidPos, cam->GetPos(), unit->GetDrawRadius()))
		return false;

	return (cam->InView(unit->drawMidPos, unit->GetDrawRadius()));
}

bool CUnitDrawer::CanDrawOpaqueUnitShadow(const CUnit* unit) const
{
	if (unit->noDraw)
		return false;
	if (unit->IsInVoid())
		return false;
	// no shadow if unit is already an icon from player's POV
	if (unit->isIcon)
		return false;
	if (unit->isCloaked)
		return false;

	const CCamera* cam = CCameraHandler::GetActiveCamera();

	const bool unitInLOS = ((unit->losStatus[gu->myAllyTeam] & LOS_INLOS) || gu->spectatingFullView);
	const bool unitInView = cam->InView(unit->drawMidPos, unit->GetDrawRadius());

	return (unitInLOS && unitInView);
}




bool CUnitDrawerGL4::CheckLegacyDrawing(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall)
{
	if (forceLegacyPath)
		return false;

	if (lodCall || preList != 0 || postList != 0 || unit->luaDraw || !noLuaCall) { //TODO: sanitize
		ForceLegacyPath();
		return false;
	}

	return true;
}