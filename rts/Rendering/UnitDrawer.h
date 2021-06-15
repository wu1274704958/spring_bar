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
	virtual ~CUnitDrawer() = 0;
public:
	template<typename T>
	static CUnitDrawer* GetInstance() {
		static T instance;
		return &instance;
	}

	static void InitStatic();
	static void KillStatic(bool reload);

	static void ForceLegacyPath() {
		forceReselection = true;
		forceLegacyPath = true;
		SelectImplementation(); //do immeditely?
	}

	static void SelectImplementation();
	static void SelectImplementation(int targetImplementation);

	// Set/Get state from outside
	static void SetDrawForwardPass(bool b) { drawForward = b; }
	static void SetDrawDeferredPass(bool b) { drawDeferred = b; }
	static bool DrawForward() { return drawForward; }
	static bool DrawDeferred() { return drawDeferred; }

	static bool UseAdvShading() { return advShading; }
	static bool& UseAdvShadingRef() { forceReselection = true; return advShading; }
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
	virtual bool CanEnable() const { return false; }
	virtual bool CanDrawAlpha() const { return false; }
	virtual bool CanDrawDeferred() const { return false; }

	virtual bool IsLegacy() const { return true; }

	// Setup Fixed State
	virtual void SetupOpaqueDrawing(bool deferredPass) = 0;
	virtual void ResetOpaqueDrawing(bool deferredPass) = 0;
	virtual void SetupAlphaDrawing(bool deferredPass) = 0;
	virtual void ResetAlphaDrawing(bool deferredPass) = 0;

	// alpha.x := alpha-value
	// alpha.y := alpha-pass (true or false)
	virtual bool SetTeamColour(int team, const float2 alpha = float2(1.0f, 0.0f)) const = 0;

	// DrawUnit*
	virtual void DrawUnitModel(const CUnit* unit, bool noLuaCall) = 0;
	virtual void DrawUnitModelBeingBuiltShadow(const CUnit* unit, bool noLuaCall) = 0;
	virtual void DrawUnitModelBeingBuiltOpaque(const CUnit* unit, bool noLuaCall) = 0;
	virtual void DrawUnitNoTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) = 0;
	virtual void DrawUnitTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) = 0;
	virtual void DrawIndividual(const CUnit* unit, bool noLuaCall) = 0;
	virtual void DrawIndividualNoTrans(const CUnit* unit, bool noLuaCall) = 0;

	// DrawIndividualDef*
	virtual void DrawIndividualDefOpaque(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) = 0;
	virtual void DrawIndividualDefAlpha(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) = 0;

	// Draw*
	virtual void Draw(bool drawReflection, bool drawRefraction = false) = 0;
	virtual void DrawOpaquePass(bool deferredPass, bool drawReflection, bool drawRefraction) = 0;
	virtual void DrawShadowPass() = 0;
	virtual void DrawAlphaPass() = 0;

	// Icons Minimap
	virtual void DrawUnitMiniMapIcons() const = 0;
	virtual void UpdateUnitDefMiniMapIcons(const UnitDef* ud) = 0;

	// Icons Map
	virtual void DrawUnitIcons() = 0;
	virtual void DrawUnitIconsScreen() = 0;
	//virtual void DrawUnitMiniMapIcon(const CUnit* unit, CVertexArray* va) const = 0;

	// Build Squares
	        bool ShowUnitBuildSquare(const BuildInfo& buildInfo) { return ShowUnitBuildSquare(buildInfo, std::vector<Command>()); }
	virtual bool ShowUnitBuildSquare(const BuildInfo& buildInfo, const std::vector<Command>& commands) = 0;
protected:
	virtual void Enable(bool deferredPass, bool alphaPass) = 0;
	virtual void Disable(bool deferredPass) = 0;

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
	inline static bool advShading = false;
	inline static bool wireFrameMode = false;

	/// <summary>
	/// .x := regular unit alpha
	/// .y := ghosted unit alpha (out of radar)
	/// .z := ghosted unit alpha (inside radar)
	/// .w := AI-temp unit alpha
	/// </summary>
	inline static float4 alphaValues = {};
private:
	inline static bool forceReselection = true;
	inline static int selectedImplementation = UnitDrawerTypes::UNIT_DRAWER_FFP;
	inline static GL::LightHandler lightHandler;
	inline static GL::GeometryBuffer* geomBuffer = nullptr;
protected:
	CUnitDrawerData unitDrawerData;
};

class CUnitDrawerLegacy : public CUnitDrawer {
public:
	// Inherited via CUnitDrawer
	virtual void SetupOpaqueDrawing(bool deferredPass) override;
	virtual void ResetOpaqueDrawing(bool deferredPass) override;

	virtual void SetupAlphaDrawing(bool deferredPass) override;
	virtual void ResetAlphaDrawing(bool deferredPass) override;

	virtual bool SetTeamColour(int team, const float2 alpha = float2(1.0f, 0.0f)) const override;

	virtual void DrawUnitModel(const CUnit* unit, bool noLuaCall) override;
};

class CUnitDrawerFFP  : public CUnitDrawerLegacy {
public:
	CUnitDrawerFFP() {}
	virtual ~CUnitDrawerFFP() = 0;
public:
	// Inherited via CUnitDrawer
	virtual void SunChanged() override {};

	virtual bool SetTeamColour(int team, const float2 alpha = float2(1.0f, 0.0f)) const override;
	virtual void DrawUnitModelBeingBuiltShadow(const CUnit* unit, bool noLuaCall) override;
	virtual void DrawUnitModelBeingBuiltOpaque(const CUnit* unit, bool noLuaCall) override;
	virtual void DrawUnitNoTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) override;
	virtual void DrawUnitTrans(const CUnit* unit, unsigned int preList, unsigned int postList, bool lodCall, bool noLuaCall) override;
	virtual void DrawIndividual(const CUnit* unit, bool noLuaCall) override;
	virtual void DrawIndividualNoTrans(const CUnit* unit, bool noLuaCall) override;
	virtual void DrawIndividualDefOpaque(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) override;
	virtual void DrawIndividualDefAlpha(const SolidObjectDef* objectDef, int teamID, bool rawState, bool toScreen = false) override;
	virtual void Draw(bool drawReflection, bool drawRefraction = false) override;
	virtual void DrawOpaquePass(bool deferredPass, bool drawReflection, bool drawRefraction) override;
	virtual void DrawShadowPass() override;
	virtual void DrawAlphaPass() override;
	virtual void DrawUnitMiniMapIcons() const override;
	virtual void UpdateUnitDefMiniMapIcons(const UnitDef* ud) override;
	virtual void DrawUnitIcons() override;
	virtual void DrawUnitIconsScreen() override;
	virtual bool ShowUnitBuildSquare(const BuildInfo& buildInfo, const std::vector<Command>& commands) override;
	virtual void Enable(bool deferredPass, bool alphaPass) override;
	virtual void Disable(bool deferredPass) override;
	virtual void SetNanoColor(const float4& color) const override;
};

class CUnitDrawerARB  : public CUnitDrawer {};
class CUnitDrawerGLSL : public CUnitDrawer {};
class CUnitDrawerGL4  : public CUnitDrawer {};


extern CUnitDrawer* unitDrawer;
