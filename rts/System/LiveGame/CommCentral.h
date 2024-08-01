#if ENABLE_LIVE_GAME

#include "Net/LocalMemComm/common.h"
#include "Net/LocalMemComm/local_mem_proto.h"
#include "Net/LocalMemComm/local_mem_adapter.h"
#include "Net/LocalMemComm/local_mem_comm.h"
#include "Net/LocalMemComm/test_component.h"
#include <memory>

class CLocalCommCentralErrorHandler {
public:
	void error(const std::string& msg);
};

class CCommCentral {
public:
	using CommTy = eqd::LocalMemComm<eqd::local_mem_proto<DefChecksum>, DefStringSerializer, DefStringSerializer, eqd::win_local_mem_adapter, CLocalCommCentralErrorHandler>;
	CCommCentral() {}
	CCommCentral(const CCommCentral&) = delete;
	CCommCentral(CCommCentral&&) = delete;


	bool Init(const std::string& memKey, uint32_t size);
	void tick();
	std::optional<std::string> PopMsg();
	bool HasMsg();
	void SendMsg(const std::string& msg);
	bool IsInit();
	void Destroy(bool force = false);
private:
	std::shared_ptr<CommTy>	comm;
};

#endif //ENABLE_LIVE_GAME