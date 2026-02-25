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

bool encodeCommand(Command command, bool isAck, uint8_t& outCommandByte);
bool decodeCommand(uint8_t commandByte, Command& outCommand, bool& outIsAck);
Packet makePacket(Command command);
Packet makeAck(Command command);
bool parsePacket(const Packet& packet, Command& outCommand, bool& outIsAck);

}  // namespace protocol
