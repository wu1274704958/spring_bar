/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <sstream>
#include "System/Log/ILog.h"

struct GlobalRNGLog {
	static bool DoLog();
	template<typename ...Args>
	static constexpr void MyCondLog(Args && ...args) noexcept
	{
		if (!DoLog())
			return;

		std::ostringstream ss;
		((ss << std::forward<Args>(args) << " "), ...);
		LOG("%s", ss.str().c_str());
	}
};