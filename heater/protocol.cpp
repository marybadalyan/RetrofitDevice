#include "protocol.h"

namespace protocol {

uint8_t checksum(uint8_t command) {
    return static_cast<uint8_t>(command ^ 0xA5);
}

Packet makePacket(Command command) {
    const uint8_t cmd = static_cast<uint8_t>(command);
    return Packet{kHeader, cmd, checksum(cmd)};
}

bool parsePacket(const Packet& packet, Command& outCommand) {
    if (packet.header != kHeader) {
        return false;
    }
    if (packet.checksum != checksum(packet.command)) {
        return false;
    }

    switch (packet.command) {
        case static_cast<uint8_t>(Command::ON):
        case static_cast<uint8_t>(Command::OFF):
        case static_cast<uint8_t>(Command::TEMP_UP):
        case static_cast<uint8_t>(Command::TEMP_DOWN):
            outCommand = static_cast<Command>(packet.command);
            return true;
        default:
            return false;
    }
}

uint8_t makeAck(Command command) {
    return static_cast<uint8_t>(command) | 0x80U;
}

}  // namespace protocol
