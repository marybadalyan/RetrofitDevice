#pragma once

#include <cstdint>

#include "commands.h"

namespace protocol {

constexpr uint16_t kHeader = 0xA55A;

struct Packet {
    uint16_t header;
    uint8_t command;
    uint8_t checksum;
};

uint8_t checksum(uint8_t command);
Packet makePacket(Command command);
bool parsePacket(const Packet& packet, Command& outCommand);
uint8_t makeAck(Command command);

}  // namespace protocol
