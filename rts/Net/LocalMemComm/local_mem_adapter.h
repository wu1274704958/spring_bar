#pragma once

#include <numeric>
#include <string>
#include "common.h"

#ifdef _WIN32
#include <windows.h>
namespace eqd {

	class win_local_mem_adapter {
	public:
		bool init(const std::string& mem_id, uint32_t size);
		void release();
		const std::string& last_error();
		uint8_t* get_mem();
		bool is_mem_idle();
		bool try_lock_mem();
		bool try_wait_lock_mem();
		void unlock_mem();
	protected:
		bool create_event(const std::string& mem_id);
		bool create_mutex(const std::string& mem_id);
	private:
		uint8_t* ptr = nullptr;
		uint32_t size = 0;
		std::string _last_error;
		::HANDLE mem_handle = NULL;
		::HANDLE event_handle = NULL;
		::HANDLE mutex_handle = NULL;
	};

}


#endif // WIN32
