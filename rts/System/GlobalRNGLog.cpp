/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GlobalRNGLog.h"
#include "Sim/Misc/GlobalSynced.h"
#include "System/Platform/CrashHandler.h"

bool GlobalRNGLog::DoLog()
{
	return (gs->frameNum >= 9624 && gs->frameNum <= 9625);
}

void GlobalRNGLog::StackTrace() noexcept
{
	CrashHandler::OutputStacktrace();
}
