#include "protocol.h"

#include "prefferences.h"

namespace protocol {

namespace {
constexpr uint8_t invertByte(uint8_t value) {
    return static_cast<uint8_t>(~value);
}

bool isAddressMatch(const Packet& packet) {
    const uint8_t expectedLow = static_cast<uint8_t>(kNecDeviceAddress & 0xFFU);
    const uint8_t expectedHigh = static_cast<uint8_t>((kNecDeviceAddress >> 8) & 0xFFU);

    const bool isClassicNec = (packet.addressInverse == invertByte(packet.address));
    if (isClassicNec) {
        return (packet.address == expectedLow);
    }

    // Extended NEC: first two bytes are the full 16-bit address.
    return (packet.address == expectedLow) && (packet.addressInverse == expectedHigh);
}
}  // namespace

bool encodeCommand(Command command, uint8_t& outCommandByte) {
    switch (command) {
        case Command::ON:
            outCommandByte = kNecCommandOn;
            return true;
        case Command::OFF:
            outCommandByte = kNecCommandOff;
            return true;
        case Command::TEMP_UP:
            outCommandByte = kNecCommandTempUp;
            return true;
        case Command::TEMP_DOWN:
            outCommandByte = kNecCommandTempDown;
            return true;
        default:
            return false;
    }
}

bool decodeCommand(uint8_t commandByte, Command& outCommand) {
    if (commandByte == kNecCommandOn) {
        outCommand = Command::ON;
        return true;
    }
    if (commandByte == kNecCommandOff) {
        outCommand = Command::OFF;
        return true;
    }
    if (commandByte == kNecCommandTempUp) {
        outCommand = Command::TEMP_UP;
        return true;
    }
    if (commandByte == kNecCommandTempDown) {
        outCommand = Command::TEMP_DOWN;
        return true;
    }
    return false;
}

Packet makePacket(Command command) {
    uint8_t encodedCommand = 0;
    if (!encodeCommand(command, encodedCommand)) {
        return Packet{0, 0, 0, 0};
    }

    const uint8_t addressLow = static_cast<uint8_t>(kNecDeviceAddress & 0xFFU);
    const uint8_t addressHigh = static_cast<uint8_t>((kNecDeviceAddress >> 8) & 0xFFU);
    const bool isClassicAddress = (addressHigh == invertByte(addressLow));
    const uint8_t addressSecondByte = isClassicAddress ? invertByte(addressLow) : addressHigh;

    return Packet{
        addressLow,
        addressSecondByte,
        encodedCommand,
        invertByte(encodedCommand),
    };
}

bool parsePacket(const Packet& packet, Command& outCommand) {
    if (!isAddressMatch(packet)) {
        return false;
    }
    if (packet.commandInverse != invertByte(packet.command)) {
        return false;
    }

    return decodeCommand(packet.command, outCommand);
}

}  // namespace protocol
