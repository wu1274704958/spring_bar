#pragma once

#include "common.h"
#include <vector>
#include <string>
#include <numeric>
#include <functional>
//#include <iostream>

namespace eqd {

	// CS : checksum
	template<typename CS>
	class local_mem_proto {
		static constexpr uint8_t ST_IDLE = 0;
		static constexpr uint32_t HeaderSize = 12;
		static constexpr uint32_t ClearMsgOffset = 2;
	public:
		//[Count:1][MsgId:1][SendId:1][RecvCount:1][Len:4][Checksum:4][Data:Len]
		LMC_state try_send(uint8_t* ptr, uint32_t size, const std::vector<uint8_t>& buf)
		{
			if (send_id == 0) return LMC_state::Uninit;
			if (buf.size() + HeaderSize > size)
			{
				_last_error = "message too large";
				return LMC_state::Failed;
			}
			if (ptr[2] == ST_IDLE && ptr[3] == 0)
			{
				ptr[1] = ptr[1] + 1;
				if (ptr[1] == 0)
					ptr[1] = 1;
//				std::cout << "<<< send id = " << (int)ptr[1] << std::endl;
				ptr[2] = send_id;
				ptr[3] = 0;
				write_data(ptr + 4, static_cast<uint32_t>(buf.size()));
				write_data(ptr + 4 + sizeof(uint32_t), static_cast<uint32_t>(CS::checksum(buf)));
				memcpy(ptr + 4 + sizeof(uint32_t) * 2, buf.data(),buf.size());
				return LMC_state::Success;
			}
			return LMC_state::Busy;
		}

		LMC_state try_recv(uint8_t* ptr, uint32_t size, std::function<void(uint8_t*, uint32_t)> callback)
		{
			if (send_id == 0) return LMC_state::Uninit;
			if (ptr[2] == ST_IDLE && ptr[3] == 0)
				return LMC_state::Idle;
			if (ptr[2] == send_id || last_recv_msg_id == ptr[1])
			{	
				//readed count > sum - 1 can clear 
				if (ptr[3] >= ptr[0] - 1)
					set_idle(ptr, size);
				return LMC_state::Busy;
			}
//			std::cout << ">>> recv id = " << (int)ptr[1] << std::endl;
			last_recv_msg_id = ptr[1];
			uint32_t msg_len = read_data<uint32_t>(ptr + 4);
			uint32_t checksum = read_data<uint32_t>(ptr + 4 + sizeof(uint32_t));
			if (msg_len + HeaderSize > size)
			{
				_last_error = "message size > memory size";
				return LMC_state::Failed;
			}
			if (checksum != CS::checksum(ptr + 4 + sizeof(uint32_t) * 2, msg_len))
			{
				_last_error = "message check sum failed";
				return LMC_state::Failed;
			}
			callback(ptr + 4 + sizeof(uint32_t) * 2, msg_len);
			// readed == count - 1
			if (ptr[3] + 1 == ptr[0] - 1)
				set_idle(ptr, size);
			else
				sign(ptr, size);
			return LMC_state::Success;
		}
		const std::string& last_error() { return _last_error; }
		LMC_state set_idle(uint8_t* ptr, uint32_t size)
		{
			memset(ptr + ClearMsgOffset, 0, size - ClearMsgOffset);
			return LMC_state::Success;
		}
		LMC_state sign(uint8_t* ptr, uint32_t size)
		{
			ptr[3] = ptr[3] + 1;
			return LMC_state::Success;
		}

		bool init(uint8_t* ptr, uint32_t size)
		{
			if (ptr[0] == 255)
			{
				_last_error = "This memory is max count";
				return false;
			}
			send_id = ptr[0] = ptr[0] + 1;
			return true;
		}
		
		void release(uint8_t* ptr, uint32_t size){
			ptr[0] = ptr[0] - 1;
			send_id = 0;
		}

	private:
		template<typename T>
		void write_data(uint8_t* ptr, T v)
		{
			memcpy(ptr, &v, sizeof(T));
		}

		template<typename T>
		T read_data(uint8_t* ptr)
		{
			T v;
			memcpy(&v, ptr, sizeof(T));
			return v;
		}

		std::string _last_error;
		uint8_t send_id = 0;
		uint8_t last_recv_msg_id = 0;
	};

}