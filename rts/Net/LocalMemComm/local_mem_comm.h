#include <numeric>
#include <string>
#include <functional>
#include <list>
#include <assert.h>
#include <optional>

#include "common.h"
namespace eqd {

	// P : proto 
	// S: serializer 
	// DS: deserializer 
	// A: local memory adapter
	// EH: error handler
	template<typename P, typename S, typename DS, typename A, typename EH>
	class LocalMemComm {
		using DSTy = typename DS::DSType;
		using STy = typename S::SType;

	public:
		LocalMemComm(const std::string& mem_id, uint32_t size) : mem_id(std::move(mem_id)), size(size)
		{
			init_adapter = adapter.init(this->mem_id, size);
			if (!init_adapter)
			{
				std::string error = adapter.last_error();
				EH::error(std::move(error));
				return;
			}
			adapter.try_wait_lock_mem();
			init_proto = proto.init(adapter.get_mem(), size);
			if (!init_proto)
			{
				std::string error = proto.last_error();
				EH::error(std::move(error));
			}
			adapter.unlock_mem();
		}
		~LocalMemComm()
		{
			if (init_proto && init_adapter)
			{
				adapter.try_wait_lock_mem();
				proto.release(adapter.get_mem(), size);
				adapter.unlock_mem();
			}
			if(init_adapter)
				adapter.release();
			init_proto = init_adapter = false;
		}
		LocalMemComm(const LocalMemComm&) = delete;
		LocalMemComm(LocalMemComm&&) = delete;
		LocalMemComm& operator=(const LocalMemComm&) = delete;
		LocalMemComm& operator=(LocalMemComm&&) = delete;

		bool tick()
		{
			if (!init_success()) return false;

			LMC_state state = try_recv([this](uint8_t* msg, uint32_t len) { on_recv_msg(msg, len); });
			if (state == LMC_state::Success)
			{
				try_send();
			}
			else if (state == LMC_state::Idle) {
				try_send();
			}

			return !recv_buf.empty();
		}
		bool has_recv() const { return !recv_buf.empty(); }
		std::optional<DSTy> pop_recv()
		{
			if (!recv_buf.empty())
			{
				std::optional ret(recv_buf.front());
				recv_buf.pop_front();
				return ret;
			}
			return std::nullopt;
		}

		void send(const STy& data)
		{
			if (!init_success()) return;
			const std::vector<uint8_t> msg_data = serializer.serialize(data);
			assert(data.size() > 0);
			if (send_buf.empty())
			{
				bool succ = try_send(msg_data);
				if (!succ)
				{
					send_buf.push_back(std::move(msg_data));
				}
			}
			else {
				send_buf.push_back(std::move(msg_data));
			}
		}

		bool init_success() const
		{
			return init_adapter && init_proto;
		}

		bool has_unsend() const
		{
			return !send_buf.empty();
		}
	protected:

		void on_recv_msg(uint8_t* ptr, uint32_t size)
		{
			DSTy data = deserializer.deserialize(ptr, size);
			recv_buf.push_back(std::move(data));
		}

		bool try_send(const std::vector<uint8_t>& buf)
		{
			if (adapter.try_lock_mem())
			{
				uint8_t* ptr = adapter.get_mem();
				LMC_state state = proto.try_send(ptr, size, buf);
				if (state == LMC_state::Failed)
				{
					const std::string& error = proto.last_error();
					EH::error(error);
				}
				adapter.unlock_mem();
				return state == LMC_state::Success;
			}
			return false;
		}

		bool try_send()
		{
			if (need_send())
			{
				auto& data = send_buf.front();
				if (try_send(data))
				{
					send_buf.pop_front();
					return true;
				}
				return false;
			}
			return false;
		}

		LMC_state try_recv(std::function<void(uint8_t*,uint32_t)> callback)
		{
			if (adapter.try_lock_mem())
			{
				uint8_t* ptr = adapter.get_mem();
				LMC_state state = proto.try_recv(ptr,size, callback);
				if (state == LMC_state::Failed)
				{
					const std::string& error = proto.last_error();
					EH::error(error);
				}
				adapter.unlock_mem();
				return state;
			}
			return LMC_state::Busy;
		}

		bool need_send() const
		{
			return !send_buf.empty();
		}

	private:
		P proto;
		S serializer;
		DS deserializer;
		A adapter;
		bool init_adapter = false;
		bool init_proto = false;

		std::list<std::vector<uint8_t>> send_buf;
		std::list<DSTy> recv_buf;

		std::string mem_id;
		uint32_t size;

	};

}