/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include <memory>

#include "System/EventClient.h"
#include "System/float3.h"
#include "Rendering/Models/ModelRenderContainer.h"
#include "Rendering/UnitDefImage.h"

struct SolidObjectGroundDecal;
struct S3DModel;
class CUnitDrawer;

namespace icon {
	class CIconData;
}
namespace GL {
	struct GeometryBuffer;
}

struct GhostSolidObject {
public:
	void IncRef() { (refCount++); }
	bool DecRef() { return ((refCount--) > 1); }

public:
	SolidObjectGroundDecal* decal; //FIXME defined in legacy decal handler with a lot legacy stuff
	S3DModel* model;

	float3 pos;
	float3 dir;

	int facing; //FIXME replaced with dir-vector just legacy decal drawer uses this
	int team;
	int refCount;
	int lastDrawFrame;
};

class CUnitDrawerData : public CEventClient {
public:
	// CEventClient interface
	bool WantsEvent(const std::string & eventName) {
		return
			eventName == "RenderUnitCreated" || eventName == "RenderUnitDestroyed" ||
			eventName == "UnitCloaked" || eventName == "UnitDecloaked" ||
			eventName == "UnitEnteredRadar" || eventName == "UnitEnteredLos" ||
			eventName == "UnitLeftRadar" || eventName == "UnitLeftLos" ||
			eventName == "PlayerChanged" || eventName == "SunChanged";
	}
	bool GetFullRead() const { return true; }
	int GetReadAllyTeam() const { return AllAccessTeam; }

	void RenderUnitCreated(const CUnit* unit, int cloaked);
	void RenderUnitDestroyed(const CUnit* unit);

	void UnitEnteredRadar(const CUnit* unit, int allyTeam);
	void UnitLeftRadar(const CUnit* unit, int allyTeam) { UnitEnteredRadar(unit, allyTeam); };

	void UnitEnteredLos(const CUnit* unit, int allyTeam);
	void UnitLeftLos(const CUnit * unit, int allyTeam);

	void UnitCloaked(const CUnit * unit);
	void UnitDecloaked(const CUnit * unit);

	void PlayerChanged(int playerNum);
	void SunChanged();
public:
	struct TempDrawUnit {
		const UnitDef* unitDef;

		int team;
		int facing;
		int timeout;

		float3 pos;
		float rotation;

		bool drawAlpha;
		bool drawBorder;
	};
public:
	CUnitDrawerData();
	virtual ~CUnitDrawerData();
public:
	void SetUnitDrawDist(float dist) {
		unitDrawDist = dist;
		unitDrawDistSqr = unitDrawDist * unitDrawDist;
	}
	void SetUnitIconDist(float dist) {
		unitIconDist = dist;
		iconLength = unitIconDist * unitIconDist * 750.0f;
	}

	// IconsAsUI
	float GetUnitIconScaleUI() { return iconScale; }
	float GetUnitIconFadeStart() { return iconFadeStart; }
	float GetUnitIconFadeVanish() { return iconFadeVanish; }
	void SetUnitIconScaleUI(float scale) { iconScale = std::clamp(scale, 0.5f, 2.0f); }
	void SetUnitIconFadeStart(float scale) { iconFadeStart = std::clamp(scale, 1.0f, 10000.0f); }
	void SetUnitIconFadeVanish(float scale) { iconFadeVanish = std::clamp(scale, 1.0f, 10000.0f); }

	// *UnitDefImage
	void SetUnitDefImage(const UnitDef* unitDef, const std::string& texName);
	void SetUnitDefImage(const UnitDef* unitDef, unsigned int texID, int xsize, int ysize);
	uint32_t GetUnitDefImage(const UnitDef* unitDef);
public:
	void AddTempDrawUnit(const TempDrawUnit& tempDrawUnit);

	void Update();
	void UpdateGhostedBuildings();
public:
	const std::vector<CUnit*>& GetUnsortedUnits() const { return unsortedUnits; }

	ModelRenderContainer<CUnit>& GetOpaqueModelRenderer(int modelType) { return opaqueModelRenderers[modelType]; }
	ModelRenderContainer<CUnit>& GetAlphaModelRenderer(int modelType) { return alphaModelRenderers[modelType]; }

	const std::vector<UnitDefImage>& GetUnitDefImages() const { return unitDefImages; }
	      std::vector<UnitDefImage>& GetUnitDefImages()       { return unitDefImages; }
private:
	const icon::CIconData* GetUnitIcon(const CUnit* unit);

	void UpdateTempDrawUnits(std::vector<TempDrawUnit>& tempDrawUnits);

	void UpdateUnitMiniMapIcon(const CUnit* unit, bool forced, bool killed);
	void UpdateUnitIconState(CUnit* unit);
	void UpdateUnitIconStateScreen(CUnit* unit);
	static void UpdateUnitDrawPos(CUnit* unit);

	/// Returns true if the given unit should be drawn as icon in the current frame.
	bool DrawAsIcon(const CUnit* unit, const float sqUnitCamDist) const;
	bool DrawAsIconScreen(CUnit* unit) const;
public:
	// lenghts & distances
	float unitDrawDist;
	float unitIconDist;
	float iconLength;

	//icons
	bool iconHideWithUI = true;

	// IconsAsUI
	bool useScreenIcons = false;
private:
	std::array<ModelRenderContainer<CUnit>, MODELTYPE_CNT> opaqueModelRenderers;
	std::array<ModelRenderContainer<CUnit>, MODELTYPE_CNT> alphaModelRenderers;

	/// units being rendered (note that this is a completely
	/// unsorted set of 3DO, S3O, opaque, and cloaked models!)
	std::vector<CUnit*> unsortedUnits;

	/// AI unit ghosts
	std::array< std::vector<TempDrawUnit>, MODELTYPE_CNT> tempOpaqueUnits;
	std::array< std::vector<TempDrawUnit>, MODELTYPE_CNT> tempAlphaUnits;

	/// buildings that were in LOS_PREVLOS when they died and not in LOS since
	std::vector<std::array<std::vector<GhostSolidObject*>, MODELTYPE_CNT>> deadGhostBuildings;
	/// buildings that left LOS but are still alive
	std::vector<std::array<std::vector<CUnit*>, MODELTYPE_CNT>> liveGhostBuildings;

	/// units that are only rendered as icons this frame
	std::vector<CUnit*> iconUnits;

	spring::unsynced_map<icon::CIconData*, std::vector<const CUnit*> > unitsByIcon;

	std::vector<UnitDefImage> unitDefImages;

	// lenghts & distances
	float unitDrawDistSqr;

	// icons
	bool useDistToGroundForIcons;
	float sqCamDistToGroundForIcons;



	// IconsAsUI
	static constexpr float iconSizeMult = 0.005f; // 1/200
	float iconSizeBase = 32.0f;
	float iconScale = 1.0f;
	float iconFadeStart = 3000.0f;
	float iconFadeVanish = 1000.0f;
	float iconZoomDist;
};