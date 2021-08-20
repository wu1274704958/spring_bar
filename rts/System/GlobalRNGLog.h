/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <sstream>
#include "System/Log/ILog.h"

struct GlobalRNGLog {
	static bool DoLog();
	template<typename ...Args>
	static constexpr void MyCondLog(bool synced, Args && ...args) noexcept
	{
		if (!synced || !DoLog())
			return;

		std::ostringstream ss;
		((ss << std::forward<Args>(args) << " "), ...);
		LOG("GlobalRNGLog::MyCondLog: %s", ss.str().c_str());
	}
};