/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef MOVE_TYPE_COMPONENTS_H__
#define MOVE_TYPE_COMPONENTS_H__

#include "System/Ecs/Components/BaseComponents.h"

#include <System/Threading/ThreadPool.h>

struct CUnit;
struct CFeature;

namespace MoveTypes {

// For move types that need to be handled single threaded.
ALIAS_COMPONENT(GeneralMoveType, int);

// Special multi-thread ground move type.
ALIAS_COMPONENT(GroundMoveType, int);

// Used by units that have updated the ground collision map and may have trapped units as a result.
// This is used to allow such a situation to be detected immediately. The fall-back checks are too
// slow in practice.
enum UnitTrapCheckType {
    TRAPPER_IS_UNIT,
    TRAPPER_IS_FEATURE
};
struct UnitTrapCheck {
    UnitTrapCheckType type;
    int id;
};

template<class Archive>
void serialize(Archive &ar, UnitTrapCheck &c) { ar(c.type, c.id); }

template<class Archive, class Snapshot>
void serializeComponents(Archive &archive, Snapshot &snapshot) {
    snapshot.template component
        < GeneralMoveType, GroundMoveType, UnitTrapCheck
        >(archive);
}

// These components do not need to be saved. They are temporary.. and actually in the ecs so maybe
// they should be moved out of here?

struct FeatureCollisionEvent {
    CUnit* collider;
    CFeature* collidee;
    int id;

    FeatureCollisionEvent(int _id, CUnit* _collider, CFeature* _collidee)
    : id(_id)
    , collider(_collider)
    , collidee(_collidee)
    {}
};

struct FeatureCrushEvent {
    CUnit* collider;
    CFeature* collidee;
    float3 crushImpulse;
    int id;

    FeatureCrushEvent(int _id, CUnit* _collider, CFeature* _collidee, float3 _crushImpulse)
    : id(_id)
    , collider(_collider)
    , collidee(_collidee)
    , crushImpulse(_crushImpulse)
    {}
};

struct FeatureMoveEvent {
    CUnit* collider;
    CFeature* collidee;
    float3 moveImpulse;
    int id;

    FeatureMoveEvent(int _id, CUnit* _collider, CFeature* _collidee, float3 _moveImpulse)
    : id(_id)
    , collider(_collider)
    , collidee(_collidee)
    , moveImpulse(_moveImpulse)
    {}
};

struct UnitCollisionEvent {
    CUnit* collider;
    CUnit* collidee;
    int id;

    UnitCollisionEvent(int _id, CUnit* _collider, CUnit* _collidee)
    : id(_id)
    , collider(_collider)
    , collidee(_collidee)
    {}
};

struct UnitCrushEvent {
    CUnit* collider;
    CUnit* collidee;
    float3 crushImpulse;
    int id;

    UnitCrushEvent(int _id, CUnit* _collider, CUnit* _collidee, float3 _crushImpulse)
    : id(_id)
    , collider(_collider)
    , collidee(_collidee)
    , crushImpulse(_crushImpulse)
    {}
};

struct UnitMovedEvent {
    int id;

    UnitMovedEvent(int _id)
    : id(_id)
    {}
};

// struct NewWaypointsEvent {
//     CUnit* mobileUnit;
//     float3 currWaypoint;
//     float3 nextWaypoint;
//     int id;

//     NewWaypointsEvent(int _id, CUnit* _mobileUnit, const float3& _currWaypoint, const float3& _nextWaypoint)
//     : id(_id)
//     , mobileUnit(_mobileUnit)
//     , currWaypoint(_currWaypoint)
//     , nextWaypoint(_nextWaypoint)
//     {}
// };


struct GroundMoveSystemComponent {
	static constexpr std::size_t page_size = 1;
    static constexpr std::size_t INITIAL_TRAP_UNIT_LIST_ALLOC_SIZE = 64;

	std::array<std::vector<FeatureCollisionEvent>, ThreadPool::MAX_THREADS> collidedFeatures;
	std::array<std::vector<UnitCollisionEvent>,    ThreadPool::MAX_THREADS> collidedUnits;
	std::array<std::vector<FeatureCrushEvent>,     ThreadPool::MAX_THREADS> killFeatures;
	std::array<std::vector<UnitCrushEvent>,        ThreadPool::MAX_THREADS> killUnits;
	std::array<std::vector<FeatureMoveEvent>,      ThreadPool::MAX_THREADS> moveFeatures;
    std::array<std::vector<UnitMovedEvent>,        ThreadPool::MAX_THREADS> movedUnits;
};

struct YardmapTrapCheckSystemComponent {
	static constexpr std::size_t page_size = 1;
    static constexpr std::size_t INITIAL_TRAP_UNIT_LIST_ALLOC_SIZE = 8;

	std::array<std::vector<CUnit*>, ThreadPool::MAX_THREADS> trappedUnitLists;
};

}

#endif