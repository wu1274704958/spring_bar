#include "test_component.h"

uint32_t DefChecksum::checksum(const uint8_t* data, uint32_t len)
{
    uint32_t checksum = 0;
	for (int i = 0; i < len; ++i)
	{
		checksum += (uint32_t)data[i];
	}
	return checksum;
}

uint32_t DefChecksum::checksum(const std::vector<uint8_t>& buf)
{
	return checksum(buf.data(), (uint32_t)buf.size());
}


DefStringSerializer::DSType DefStringSerializer::deserialize(uint8_t* msg, uint32_t msg_len)
{
	return std::string((char*)msg, msg_len);
}

std::vector<uint8_t> DefStringSerializer::serialize(const DefStringSerializer::SType& data)
{
	return std::vector<uint8_t> (data.begin(),data.end());
}