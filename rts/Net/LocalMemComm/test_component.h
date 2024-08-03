#pragma once
#include <numeric>
#include <vector>
#include <string>
#include <cstdint>

class DefChecksum {
public:
	static uint32_t checksum(const uint8_t* data, uint32_t len);
	static uint32_t checksum(const std::vector<uint8_t>& buf);
};

class DefStringSerializer {
public:
	using DSType = std::string;
	using SType = std::string;

	DSType deserialize(uint8_t* msg,uint32_t msg_len);
	std::vector<uint8_t> serialize(const SType& data);
};