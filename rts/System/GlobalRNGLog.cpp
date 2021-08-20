/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GlobalRNGLog.h"
#include "Sim/Misc/GlobalSynced.h"

bool GlobalRNGLog::DoLog()
{
	return (gs->frameNum == 9625 || gs->frameNum == -1);
}