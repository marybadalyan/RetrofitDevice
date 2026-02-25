#pragma once

#include <cstdint>

#include "commands.h"

namespace protocol {

struct Packet {
    uint8_t address;
    uint8_t addressInverse;
    uint8_t command;
    uint8_t commandInverse;
};

bool encodeCommand(Command command, uint8_t& outCommandByte);
bool decodeCommand(uint8_t commandByte, Command& outCommand);
Packet makePacket(Command command);
bool parsePacket(const Packet& packet, Command& outCommand);

}  // namespace protocol
