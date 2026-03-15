#pragma once

#include <array>
#include <cstdint>

#include "../commands.h"
#include "../scheduler/scheduler.h"
#include "../time/wall_clock.h"
#include "hub_receiver.h"

class HubMockScheduler {
public:
    void tick(uint32_t nowMs, const WallClockSnapshot& wallNow, HubReceiver& hubReceiver, bool enabled);

private:
    struct Entry {
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        uint8_t weekdayMask = kWeekdayAll;
        Command command = Command::NONE;
        uint32_t lastFiredDateKey = 0;

        constexpr Entry() = default;
        constexpr Entry(uint8_t hourIn,
                        uint8_t minuteIn,
                        uint8_t secondIn,
                        uint8_t weekdayMaskIn,
                        Command commandIn,
                        uint32_t lastFiredDateKeyIn)
            : hour(hourIn),
              minute(minuteIn),
              second(secondIn),
              weekdayMask(weekdayMaskIn),
              command(commandIn),
              lastFiredDateKey(lastFiredDateKeyIn) {}
    };

    bool bootstrapPushed_ = false;
    std::array<Entry, 2> schedule_{{
        Entry{7, 0, 0, kWeekdayAll, Command::ON, 0},
        Entry{22, 0, 0, kWeekdayAll, Command::OFF, 0},
    }};
};
