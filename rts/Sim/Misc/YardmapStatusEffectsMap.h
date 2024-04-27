/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef YardmapStatusEffectsMap_H
#define YardmapStatusEffectsMap_H

#include <cstdint>
#include <vector>

#include "Sim/Misc/GlobalConstants.h"
#include "Sim/MoveTypes/MoveDefHandler.h"
#include "Sim/Units/Unit.h"
#include "Sim/Objects/SolidObject.h"
#include "System/Log/ILog.h"

class YardmapStatusEffectsMap {

    static constexpr uint32_t zMasks[] = {0x0000FFFF, 0x00FF00FF, 0x0F0F0F0F, 0x33333333, 0x55555555};
    static constexpr uint32_t zShifts[] = {16, 8, 4, 2, 1};


public:

    // States often need above water (AB) and below water (BW) 
    enum SquareStates {
        EXIT_ONLY_AW = 0x01,
        EXIT_ONLY_BW = 0x02,
        BLOCK_BUILDING_AW = 0x04,
        BLOCK_BUILDING_BW = 0x08,

        EXIT_ONLY = (EXIT_ONLY_AW|EXIT_ONLY_BW),
        BLOCK_BUILDING = (BLOCK_BUILDING_AW|BLOCK_BUILDING_BW)
    };

    static constexpr int resolution = SPRING_FOOTPRINT_SCALE;

    uint32_t interleave(uint32_t x, uint32_t y)
    {
        for(uint32_t i = 0; i < sizeof(zMasks)/sizeof(uint32_t); i++)
        {
            x = (x | (x << zShifts[i])) & zMasks[i];
            y = (y | (y << zShifts[i])) & zMasks[i];
        }
        return x | (y << 1);
    }

    uint8_t& GetMapState(int x, int z) { return stateMap[interleave(x / resolution, z / resolution)]; }
    uint8_t& GetMapStateNative(int x, int z) { return stateMap[interleave(x, z)]; }

    bool AreAllFlagsSet(int x, int z, uint8_t flags) { return (GetMapState(x, z) & flags) == flags; }
	bool AreAnyFlagsSet(int x, int z, uint8_t flags) { return (GetMapState(x, z) & flags) != 0; }
	
    void SetFlags(int x, int z, uint8_t flags) { GetMapState(x, z) |= flags; }
    void ClearFlags(int x, int z, uint8_t flags) { GetMapState(x, z) &= ~flags; }

    void InitNewExitOnlyMap();

    typedef std::vector<uint8_t> ExitOnlyMapType;
    ExitOnlyMapType stateMap;
};

extern YardmapStatusEffectsMap exitOnlyMap;

struct ObjectCollisionMapHelper {
	bool collidesUnderWater;
	bool collidesAboveWater;

	ObjectCollisionMapHelper(const CSolidObject& object) {
		SetObjectCollisionStates(object);
	}

	ObjectCollisionMapHelper(const MoveDef& moveDef, float ypos) {
		SetMoveDefCollisionStates(moveDef, ypos);
	}

	ObjectCollisionMapHelper(const MoveDef& moveDef) {
		SetMoveDefCollisionStates(moveDef);
	}

	float GetMoveCollisionHeight(const CSolidObject& object) const {
		if (object.moveDef != nullptr)
			return object.moveDef->height;
		else
			return object.height;
	}

	bool IsOnWaterSurface(const CSolidObject& object) const {
		if (object.moveDef != nullptr)
			return !object.moveDef->isSubmersible;
		else {
			auto unit = dynamic_cast<const CUnit*>(&object);
			if (unit != nullptr)
				return unit->FloatOnWater();
		}
		return false;
	}

	void SetObjectCollisionStates(const CSolidObject& object) {
		const bool floatsOnWater = IsOnWaterSurface(object);
		collidesUnderWater = !floatsOnWater;
		collidesAboveWater = (floatsOnWater || object.pos.y + GetMoveCollisionHeight(object) >= 0.f);
	}

    void SetMoveDefCollisionStates(const MoveDef& moveDef, float ypos) {
		const bool floatsOnWater = !moveDef.isSubmersible;
		collidesUnderWater = !floatsOnWater;
		collidesAboveWater = (floatsOnWater || ypos + moveDef.height >= 0.f);
    }

    void SetMoveDefCollisionStates(const MoveDef& moveDef) {
		collidesUnderWater = moveDef.isSubmersible;
		collidesAboveWater = true;
    }

	uint8_t GetExitOnlyFlags() const {
		return (YardmapStatusEffectsMap::EXIT_ONLY_BW * collidesUnderWater)
			+ (YardmapStatusEffectsMap::EXIT_ONLY_AW * collidesAboveWater);
	}

    bool IsExitOnlyAt(int x, int z) const {
		return exitOnlyMap.AreAllFlagsSet(x, z, GetExitOnlyFlags());
	}

	void SetExitOnlyAt(int x, int z) const {
		exitOnlyMap.SetFlags(x, z, GetExitOnlyFlags());
	}

	void ClearExitOnlyAt(int x, int z) const {
		exitOnlyMap.ClearFlags(x, z, GetExitOnlyFlags());
	}

	uint8_t GetBlockBuildingFlags() const {
		return (YardmapStatusEffectsMap::BLOCK_BUILDING_BW * collidesUnderWater)
			+ (YardmapStatusEffectsMap::BLOCK_BUILDING_AW * collidesAboveWater);
	}

    bool IsBlockBuildingAt(int x, int z) const {
		return exitOnlyMap.AreAllFlagsSet(x, z, GetBlockBuildingFlags());
	}

	void SetBlockBuildingAt(int x, int z) const {
		exitOnlyMap.SetFlags(x, z, GetBlockBuildingFlags());
	}

	void ClearBlockBuildingAt(int x, int z) const {
		exitOnlyMap.ClearFlags(x, z, GetBlockBuildingFlags());
	}
};

#endif