#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>

// The state acknowledged by our input queue, scoped to one streaming session.
// Transport calls remain in InputManager; failed enqueues never advance state.
class InputSessionState {
  public:
    enum class TouchPhase { Start, Move, End, None };
    enum class TouchAction { Down, Move, Up, Cancel };

    void begin(uint64_t generation) { reset(); ready_ = true; generation_ = generation; }
    void end() { reset(); }
    bool ready() const { return ready_; }
    bool current(bool active, uint64_t generation) const {
        return ready_ && active && generation == generation_;
    }

    static uint16_t maskForCount(unsigned count) {
        count = std::min(count, 16u);
        return count == 16 ? UINT16_MAX : static_cast<uint16_t>((1u << count) - 1u);
    }

    void setControllerCount(unsigned count) {
        connected_ = maskForCount(count);
        pendingRemovals_ |= announced_ & ~connected_;
        announced_ &= connected_;
        sentStates_ &= connected_;
    }

    uint16_t connectedMask() const { return connected_; }
    uint16_t announcedMask() const { return announced_; }
    uint16_t pendingRemovals() const { return pendingRemovals_; }
    void markRemoved(unsigned controller) { pendingRemovals_ &= ~bit(controller); }
    bool needsArrival(unsigned controller) const {
        return ready_ && (connected_ & bit(controller)) && !(announced_ & bit(controller)) &&
               !(pendingRemovals_ & bit(controller));
    }
    void markArrived(unsigned controller) {
        if (needsArrival(controller)) announced_ |= bit(controller);
    }
    bool canSendController(unsigned controller) const {
        return ready_ && (connected_ & announced_ & bit(controller));
    }
    bool needsInitialState(unsigned controller) const {
        return canSendController(controller) && !(sentStates_ & bit(controller));
    }
    void markStateSent(unsigned controller) {
        if (canSendController(controller)) sentStates_ |= bit(controller);
    }

    std::optional<TouchAction> touchAction(uint32_t pointer, TouchPhase phase) const {
        if (!ready_) return std::nullopt;
        const bool active = activeTouches_.count(pointer) != 0;
        switch (phase) {
            case TouchPhase::Start: return active ? TouchAction::Move : TouchAction::Down;
            // Do not forward a drag that began on an overlay or before connection.
            case TouchPhase::Move: return active ? std::optional<TouchAction>(TouchAction::Move) : std::nullopt;
            case TouchPhase::End: return active ? std::optional<TouchAction>(TouchAction::Up) : std::nullopt;
            case TouchPhase::None: return active ? std::optional<TouchAction>(TouchAction::Cancel) : std::nullopt;
        }
        return std::nullopt;
    }
    void markTouchSent(uint32_t pointer, TouchAction action) {
        if (!ready_) return;
        if (action == TouchAction::Down) activeTouches_.insert(pointer);
        else if (action == TouchAction::Up || action == TouchAction::Cancel) activeTouches_.erase(pointer);
    }
    const std::set<uint32_t>& activeTouches() const { return activeTouches_; }

  private:
    static uint16_t bit(unsigned controller) {
        return controller < 16 ? static_cast<uint16_t>(1u << controller) : 0;
    }
    void reset() {
        ready_ = false;
        generation_ = 0;
        connected_ = announced_ = sentStates_ = pendingRemovals_ = 0;
        activeTouches_.clear();
    }
    bool ready_ = false;
    uint64_t generation_ = 0;
    uint16_t connected_ = 0;
    uint16_t announced_ = 0;
    uint16_t sentStates_ = 0;
    uint16_t pendingRemovals_ = 0;
    std::set<uint32_t> activeTouches_;
};
