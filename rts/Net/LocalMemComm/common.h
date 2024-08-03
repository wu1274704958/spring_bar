#pragma once
#include <numeric>
#include <cstdio>
#include <string>
#include <cstdarg>
#include <cstdint>


namespace eqd {
	enum class LMC_state : uint8_t
	{
		Success = 0			,
		Failed				,
		Busy				,
		Idle				,
		Uninit				
	};

	std::string fmt(const char* format, ...);
}