#include "hub_receiver.h"

bool HubReceiver::pushMockCommand(Command command) {
    if (count_ >= queue_.size()) {
        return false;
    }
    queue_[tail_] = command;
    tail_ = (tail_ + 1U) % queue_.size();
    ++count_;
    return true;
}

bool HubReceiver::poll(Command& outCommand) {
    if (count_ == 0) {
        return false;
    }
    outCommand = queue_[head_];
    head_ = (head_ + 1U) % queue_.size();
    --count_;
    return true;
}
