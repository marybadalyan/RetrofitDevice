#include "hub_mock_scheduler.h"

void HubMockScheduler::tick(uint32_t nowMs,
                            const WallClockSnapshot& wallNow,
                            HubReceiver& hubReceiver,
                            bool enabled) {
    if (!enabled) {
        return;
    }

    if (!wallNow.valid || wallNow.dateKey == 0U) {
        if (!bootstrapPushed_ && nowMs > 3000) {
            hubReceiver.pushMockCommand(Command::ON);
            bootstrapPushed_ = true;
        }
        return;
    }

    const uint8_t todayBit = static_cast<uint8_t>(1U << wallNow.weekday);
    for (Entry& entry : schedule_) {
        if ((entry.weekdayMask & todayBit) == 0U) {
            continue;
        }
        if (entry.lastFiredDateKey == wallNow.dateKey) {
            continue;
        }

        const uint32_t targetSeconds =
            (static_cast<uint32_t>(entry.hour) * 3600UL) + (static_cast<uint32_t>(entry.minute) * 60UL) + entry.second;
        if (wallNow.secondsOfDay < targetSeconds) {
            continue;
        }

        if (hubReceiver.pushMockCommand(entry.command)) {
            entry.lastFiredDateKey = wallNow.dateKey;
        }
    }
}
