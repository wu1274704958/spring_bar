/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include <array>

#include "Rendering/Units/UnitDrawerData.h"
#include "Rendering/GL/LightHandler.h"
#include "System/type2.h"
#include "Sim/Units/CommandAI/Command.h"

class CSolidObject;
struct S3DModel;
struct SolidObjectDef;

enum UnitDrawerTypes {
	UNIT_DRAWER_FFP  = 0, // fixed-function path
	UNIT_DRAWER_ARB  = 1, // standard-shader path (ARB)
	UNIT_DRAWER_GLSL = 2, // standard-shader path (GLSL)
	UNIT_DRAWER_GL4  = 3, // GL4-shader path (GLSL)
	UNIT_DRAWER_CNT  = 4
};

class CUnitDrawer
{
public:
	CUnitDrawer() {}
	virtual ~CUnitDrawer() {};
public:
	template<typename T>
	static void InitInstance(int t) {
		if (unitDrawers[t] == nullptr)
			unitDrawers[t] = new T();
	}
	static void KillInstance(int t) {
		spring::SafeDelete(unitDrawers[t]);
	}

	static void InitStatic();
	static void KillStatic(bool reload);

	static void ForceLegacyPath();

	static void SelectImplementation(bool forceReselection = false);
	static void SelectImplementation(int targetImplementation);

	static void Update();

	// Set/Get state from outside
	static void SetDrawForwardPass(bool b) { drawForward = b; }
	static void SetDrawDeferredPass(bool b) { drawDeferred = b; }
	static bool DrawForward() { return drawForward; }
	static bool DrawDeferred() { return drawDeferred; }

	static bool UseAdvShading() { return advShading; }
	static bool& UseAdvShadingRef() { reselectionRequested = true; return advShading; }
	static bool& WireFrameModeRef() { return wireFrameMode; }
public:
	// Interface with CUnitDrawerData
	static void SunChangedStatic();

	//TODO kill direct access and make interfaces instead
	const CUnitDrawerData& UnitDrawerData() const {	return unitDrawerData; }
	      CUnitDrawerData& UnitDrawerData()       { return unitDrawerData; }
public:
	virtual void SunChanged() = 0;

	// Former UnitDrawerState + new functions
	virtual bool CanEnable() const = 0;
	virtual bool CanDrawAlpha() const = 0;
	virtual bool CanDrawDeferred() const = 0;

	virtual bool IsLegacy() const = 0;

	// Setup Fixed State
	virtual void SetupOpaqueDrawing(bool deferredPass) const = 0;
	virtual void ResetOpaqueDrawing(bool deferredPass) const = 0;
	virtual void SetupAlphaDrawing(bool deferredPass) const = 0;
	virtual void ResetAlphaDrawing(bool deferredPass) const = 0;

	// alpha.x := alpha-value
	// alpha.y := alpha-pass (true or false)
	virtual bool SetTeamColour(int team, const float2 alpha = float2(1.0f, 0.0f)) const = 0;

	// DrawUnit*
	virtual void DrawUnitModel(const CUnit* unit, bool noLuaCall) const = 0;
	virtual void DrawUnitModelBeingBuiltShadow(const CUnit* unit, bool noLuaCall) const = 0;
	virtual void DrawUnitModelBeingBuiltOpaque(const CUnit* unit, bool noLuaCall) const = 0;
	virtual void DrawUnitNoTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) const = 0;
	virtual void DrawUnitTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) const = 0;
	virtual void DrawIndividual(const CUnit* unit, bool noLuaCall) const = 0;
	virtual void DrawIndividualNoTrans(const CUnit* unit, bool noLuaCall) const = 0;

	// DrawIndividualDef*
	virtual void DrawIndividualDefOpaque(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) const = 0;
	virtual void DrawIndividualDefAlpha(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) const = 0;

	// Draw*
	virtual void Draw(bool drawReflection, bool drawRefraction = false) const = 0;
	virtual void DrawOpaquePass(bool deferredPass, bool drawReflection, bool drawRefraction) const = 0;
	virtual void DrawShadowPass() const = 0;
	virtual void DrawAlphaPass() const = 0;

	// Icons Minimap
	virtual void DrawUnitMiniMapIcons() const = 0;
	        void UpdateUnitDefMiniMapIcons(const UnitDef* ud) { unitDrawerData.UpdateUnitDefMiniMapIcons(ud); }

	// Icons Map
	virtual void DrawUnitIcons() const = 0;
	virtual void DrawUnitIconsScreen() const = 0;

	// Build Squares
	        bool ShowUnitBuildSquare(const BuildInfo& buildInfo) const { return ShowUnitBuildSquare(buildInfo, std::vector<Command>()); }
	virtual bool ShowUnitBuildSquare(const BuildInfo& buildInfo, const std::vector<Command>& commands) const = 0;
protected:
	bool CanDrawOpaqueUnit(const CUnit* unit, bool drawReflection, bool drawRefraction) const;
	bool CanDrawOpaqueUnitShadow(const CUnit* unit) const;

	virtual void DrawOpaqueUnitsShadow(int modelType) const = 0;
	virtual void DrawOpaqueUnits(int modelType, bool drawReflection, bool drawRefraction) const = 0;

	virtual void DrawAlphaUnits(int modelType) const = 0;

	virtual void DrawOpaqueAIUnits(int modelType) const = 0;
	virtual void DrawAlphaAIUnits(int modelType) const = 0;

	virtual void DrawGhostedBuildings(int modelType) const = 0;
protected:
	virtual void Enable(bool deferredPass, bool alphaPass) const = 0;
	virtual void Disable(bool deferredPass) const = 0;

	virtual void SetNanoColor(const float4& color) const = 0;
public:
	// lightHandler
	const GL::LightHandler* GetLightHandler() const { return &lightHandler; }
	      GL::LightHandler* GetLightHandler()       { return &lightHandler; }

	// geomBuffer
	const GL::GeometryBuffer* GetGeometryBuffer() const { return geomBuffer; }
	      GL::GeometryBuffer* GetGeometryBuffer()       { return geomBuffer; }
public:
	// Render States Push/Pop
	static void BindModelTypeTexture(int mdlType, int texType);

	static void PushModelRenderState(int mdlType);
	static void PushModelRenderState(const S3DModel* m);
	static void PushModelRenderState(const CSolidObject* o);

	static void PopModelRenderState(int mdlType);
	static void PopModelRenderState(const S3DModel* m);
	static void PopModelRenderState(const CSolidObject* o);

	// Auxilary
	static bool ObjectVisibleReflection(const float3 objPos, const float3 camPos, float maxRadius);
public:
	inline static std::array<CUnitDrawer*, UnitDrawerTypes::UNIT_DRAWER_CNT> unitDrawers = {};
public:
	inline static bool forceLegacyPath = false;

	inline static bool drawForward = true;
	inline static bool drawDeferred = false;

	inline static bool cubeMapInitialized = false;
	inline static bool advShading = true;
	inline static bool wireFrameMode = false;

	/// <summary>
	/// .x := regular unit alpha
	/// .y := ghosted unit alpha (out of radar)
	/// .z := ghosted unit alpha (inside radar)
	/// .w := AI-temp unit alpha
	/// </summary>
	inline static float4 alphaValues = {};
public:
	enum BuildStages {
		BUILDSTAGE_WIRE = 0,
		BUILDSTAGE_FLAT = 1,
		BUILDSTAGE_FILL = 2,
		BUILDSTAGE_NONE = 3,
		BUILDSTAGE_CNT = 4,
	};
private:
	inline static bool reselectionRequested = true;
	//inline static int selectedImplementation = UnitDrawerTypes::UNIT_DRAWER_FFP;
	inline static GL::LightHandler lightHandler;
	inline static GL::GeometryBuffer* geomBuffer = nullptr;
protected:
	CUnitDrawerData unitDrawerData;
};

class CUnitDrawerLegacy : public CUnitDrawer {
public:
	// caps functions
	virtual bool IsLegacy() const { return true; };
	// Inherited via CUnitDrawer
	virtual void SetupOpaqueDrawing(bool deferredPass) const override;
	virtual void ResetOpaqueDrawing(bool deferredPass) const override;

	virtual void SetupAlphaDrawing(bool deferredPass) const override;
	virtual void ResetAlphaDrawing(bool deferredPass) const override;

	virtual bool SetTeamColour(int team, const float2 alpha = float2(1.0f, 0.0f)) const override;

	virtual void DrawUnitModel(const CUnit* unit, bool noLuaCall) const override;
	virtual void DrawUnitNoTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) const override;
	virtual void DrawUnitTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) const override;

	virtual void Draw(bool drawReflection, bool drawRefraction = false) const override;
	virtual void DrawOpaquePass(bool deferredPass, bool drawReflection, bool drawRefraction) const override;
	virtual void DrawShadowPass() const override;
	virtual void DrawAlphaPass() const override;

	virtual void DrawUnitMiniMapIcons() const override;
	virtual void DrawUnitIcons() const override;
	virtual void DrawUnitIconsScreen() const override;
protected:
	virtual void DrawOpaqueUnitsShadow(int modelType) const override;
	virtual void DrawOpaqueUnits(int modelType, bool drawReflection, bool drawRefraction) const override;

	virtual void DrawAlphaUnits(int modelType) const override;

	virtual void DrawOpaqueAIUnits(int modelType) const override;
	virtual void DrawAlphaAIUnits(int modelType) const override;

	virtual void DrawGhostedBuildings(int modelType) const override;

	virtual void EnableTextures() const = 0;
	virtual void DisableTextures() const = 0;

	void DrawOpaqueUnit(CUnit* unit, bool drawReflection, bool drawRefraction) const;
	void DrawOpaqueUnitShadow(CUnit* unit) const;
	void DrawAlphaUnit(CUnit* unit, int modelType, bool drawGhostBuildingsPass) const;
	void DrawOpaqueAIUnit(const CUnitDrawerData::TempDrawUnit& unit) const;
	void DrawAlphaAIUnit(const CUnitDrawerData::TempDrawUnit& unit) const;
	void DrawAlphaAIUnitBorder(const CUnitDrawerData::TempDrawUnit& unit) const;

	virtual void DrawUnitModelBeingBuiltShadow(const CUnit* unit, bool noLuaCall) const override;
	void DrawModelWireBuildStageShadow(const CUnit* unit, const double* upperPlane, const double* lowerPlane, bool noLuaCall, bool amdHack) const;
	void DrawModelFlatBuildStageShadow(const CUnit* unit, const double* upperPlane, const double* lowerPlane, bool noLuaCall) const;
	void DrawModelFillBuildStageShadow(const CUnit* unit, const double* upperPlane, const double* lowerPlane, bool noLuaCall) const;

	virtual void DrawUnitModelBeingBuiltOpaque(const CUnit* unit, bool noLuaCall) const override;
	void DrawModelWireBuildStageOpaque(const CUnit* unit, const double* upperPlane, const double* lowerPlane, bool noLuaCall, bool amdHack) const;
	void DrawModelFlatBuildStageOpaque(const CUnit* unit, const double* upperPlane, const double* lowerPlane, bool noLuaCall) const;
	void DrawModelFillBuildStageOpaque(const CUnit* unit, const double* upperPlane, const double* lowerPlane, bool noLuaCall, bool amdHack) const;

	void PushIndividualOpaqueState(const CUnit* unit, bool deferredPass) const;
	void PushIndividualOpaqueState(const S3DModel* model, int teamID, bool deferredPass)  const;
	void PushIndividualAlphaState(const S3DModel* model, int teamID, bool deferredPass) const;

	void PopIndividualOpaqueState(const CUnit* unit, bool deferredPass) const;
	void PopIndividualOpaqueState(const S3DModel* model, int teamID, bool deferredPass) const;
	void PopIndividualAlphaState(const S3DModel* model, int teamID, bool deferredPass) const;

	virtual bool ShowUnitBuildSquare(const BuildInfo& buildInfo, const std::vector<Command>& commands) const override;

	void DrawUnitMiniMapIcon(const CUnit* unit, CVertexArray* va) const;

	static void DrawIcon(CUnit* unit, bool useDefaultIcon);
	void DrawIconScreenArray(const CUnit* unit, const icon::CIconData* icon, bool useDefaultIcon, const float dist, CVertexArray* va) const;
};

class CUnitDrawerFFP final : public CUnitDrawerLegacy {
public:
	CUnitDrawerFFP() {};
	virtual ~CUnitDrawerFFP() override {};
public:
	// Inherited via CUnitDrawer
	virtual void SunChanged() override {};

	// caps functions
	virtual bool CanEnable() const { return true; };
	virtual bool CanDrawAlpha() const { return true; };
	virtual bool CanDrawDeferred() const { return false; };

	virtual bool SetTeamColour(int team, const float2 alpha = float2(1.0f, 0.0f)) const override;

	virtual void DrawIndividual(const CUnit* unit, bool noLuaCall) const override;
	virtual void DrawIndividualNoTrans(const CUnit* unit, bool noLuaCall) const override;
	virtual void DrawIndividualDefOpaque(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) const override {};
	virtual void DrawIndividualDefAlpha (const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) const override {};

	virtual void Enable(bool deferredPass, bool alphaPass) const override;
	virtual void Disable(bool deferredPass) const override;
	virtual void SetNanoColor(const float4& color) const override;
protected:
	virtual void EnableTextures() const override;
	virtual void DisableTextures() const override;
private:
	// needed by FFP drawer-state
	static void SetupBasicS3OTexture0();
	static void SetupBasicS3OTexture1();
	static void CleanupBasicS3OTexture1();
	static void CleanupBasicS3OTexture0();
};

class CUnitDrawerARB  final : public CUnitDrawerLegacy {};
class CUnitDrawerGLSL final : public CUnitDrawerLegacy {};

class CUnitDrawerGL4  final : public CUnitDrawer {
private:
	bool CheckLegacyDrawing(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall);
};



extern CUnitDrawer* unitDrawer;