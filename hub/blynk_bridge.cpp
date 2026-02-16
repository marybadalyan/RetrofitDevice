#include "blynk_bridge.h"

void BlynkBridge::begin() {}

bool BlynkBridge::push(std::array<Command, kQueueSize>& queue, size_t& tail, size_t& count, Command command) {
    if (count >= queue.size()) {
        return false;
    }
    queue[tail] = command;
    tail = (tail + 1U) % queue.size();
    ++count;
    return true;
}

bool BlynkBridge::pop(std::array<Command, kQueueSize>& queue, size_t& head, size_t& count, Command& outCommand) {
    if (count == 0) {
        return false;
    }
    outCommand = queue[head];
    head = (head + 1U) % queue.size();
    --count;
    return true;
}

bool BlynkBridge::pushLearnRequest(Command command) {
    return push(learnQueue_, learnTail_, learnCount_, command);
}

bool BlynkBridge::pushControlCommand(Command command) {
    return push(controlQueue_, controlTail_, controlCount_, command);
}

bool BlynkBridge::pollLearnRequest(Command& outCommand) {
    return pop(learnQueue_, learnHead_, learnCount_, outCommand);
}

bool BlynkBridge::pollControlCommand(Command& outCommand) {
    return pop(controlQueue_, controlHead_, controlCount_, outCommand);
}
