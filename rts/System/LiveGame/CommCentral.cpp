#ifdef ENABLE_LIVE_GAME
#include "CommCentral.h"
#include "System/Log/ILog.h"

#endif

void CLocalCommCentralErrorHandler::error(const std::string& msg)
{
	LOG_L(L_ERROR, "LocalCommCentral error msg = %s", msg.c_str());
}

bool CCommCentral::Init(const std::string& memKey, uint32_t size)
{
	if (IsInit())
		return false;
	comm = std::make_shared<CommTy>(memKey, size);
	if (comm->init_success())
		return true;
	comm.reset();
	return false;
}

bool CCommCentral::tick()
{
	if (IsInit())
		return comm->tick();
	return false;
}

std::optional<std::string> CCommCentral::PopMsg()
{
	if (IsInit())
		return comm->pop_recv();
	return {};
}

bool CCommCentral::HasMsg()
{
	if (IsInit())
		return comm->has_recv();
	return false;
}

void CCommCentral::SendMsg(const std::string& msg)
{
	if (IsInit())
		comm->send(msg);
}

bool CCommCentral::IsInit()
{
	return comm && comm->init_success();
}

void CCommCentral::Destroy(bool force)
{
	if (!comm)
		return;
	while (!force && comm->has_unsend())
	{
		comm->tick();
	}
	comm.reset();
}






