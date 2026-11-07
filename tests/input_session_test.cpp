#include "InputSessionState.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void expect(bool value, const char* description) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
}

int main() {
    InputSessionState state;
    using Phase = InputSessionState::TouchPhase;
    using Action = InputSessionState::TouchAction;
    expect(!state.ready() && !state.current(true, 1), "input is blocked before a connection starts");
    state.setControllerCount(1);
    expect(!state.needsArrival(0) && !state.canSendController(0), "preconnection controller events are blocked");
    state.begin(1);
    state.setControllerCount(1);
    expect(state.current(true, 1) && !state.current(false, 1), "connection activity gates input");
    expect(!state.current(true, 2), "a reconnect blocks old registration before the UI sees it");
    expect(state.needsArrival(0), "neutral controller is registered without a button change");
    expect(!state.canSendController(0), "motion and state cannot precede arrival");
    state.markStateSent(0);
    expect(!state.canSendController(0), "an unregistered state does not implicitly allocate a controller");
    expect(state.needsArrival(0), "a failed arrival remains pending for retry");
    state.markArrived(0);
    expect(state.canSendController(0), "state and motion allowed after queued arrival");
    expect(state.needsInitialState(0), "initial held state is sent even when cached data matches");
    state.markStateSent(0);
    expect(!state.needsInitialState(0), "unchanged state can be suppressed after successful send");

    state.setControllerCount(5);
    expect(state.connectedMask() == 0x1f, "all five controllers are represented in the mask");
    expect(!state.needsArrival(0), "existing controller does not need a duplicate arrival");
    for (unsigned i = 1; i < 5; ++i) {
        expect(state.needsArrival(i) && !state.canSendController(i), "each newly connected controller waits for arrival");
        state.markArrived(i);
        expect(state.canSendController(i) && state.needsInitialState(i), "each controller is independently ready");
        state.markStateSent(i);
    }
    state.setControllerCount(2);
    expect(state.announcedMask() == 3, "unplugged controllers are no longer announced");
    expect(state.pendingRemovals() == 0x1c, "disconnect mask is pending until its enqueue succeeds");
    state.markRemoved(2);
    expect(state.pendingRemovals() == 0x18, "successful disconnect clears only its own pending bit");
    state.setControllerCount(2);
    expect(state.pendingRemovals() == 0x18, "failed disconnect is retained for retry");
    state.setControllerCount(5);
    expect(!state.needsArrival(4) && !state.canSendController(4),
           "rapid replug waits for the pending old-controller release");
    state.setControllerCount(2);
    state.markRemoved(3);
    state.markRemoved(4);
    expect(state.pendingRemovals() == 0, "all disconnect acknowledgments clear pending state");
    expect(!state.canSendController(4), "removed controller cannot send motion");
    state.setControllerCount(5);
    expect(state.needsArrival(4) && !state.canSendController(4), "replugged controller needs a new arrival");
    state.markArrived(4);
    expect(state.needsInitialState(4), "replugged controller needs fresh button state");
    state.begin(2);
    state.setControllerCount(5);
    expect(state.announcedMask() == 0 && state.needsArrival(0) && state.needsArrival(4),
           "reconnect with unchanged controller count resets every registration");
    expect(!state.current(true, 1) && state.current(true, 2), "new generation replaces the previous connection");
    expect(InputSessionState::maskForCount(0) == 0 && InputSessionState::maskForCount(16) == 0xffff,
           "controller mask covers its boundary cases");
    expect(InputSessionState::maskForCount(100) == 0xffff, "oversized controller count cannot overflow");
    state.markArrived(99);
    expect(!state.canSendController(99), "invalid controller index cannot shift out of bounds");

    expect(!state.touchAction(0, Phase::None), "an idle pointer does not send a spurious cancel");
    expect(!state.touchAction(0, Phase::Move), "drag that began on an overlay does not become an unpaired move");
    expect(!state.touchAction(0, Phase::End), "untracked finger lift does not send an unmatched up");
    expect(state.touchAction(0, Phase::Start) == Action::Down, "fresh pointer starts with down");
    expect(!state.touchAction(0, Phase::Move), "a failed down enqueue does not permit moves");
    state.markTouchSent(0, Action::Down);
    expect(state.activeTouches().count(0) == 1, "successful down records the pointer");
    expect(state.touchAction(0, Phase::Start) == Action::Move, "duplicate down becomes movement");
    expect(state.touchAction(0, Phase::Move) == Action::Move, "tracked pointer can move");
    expect(state.touchAction(0, Phase::End) == Action::Up, "tracked pointer ends with up");
    state.markTouchSent(0, Action::Up);
    expect(state.activeTouches().empty(), "successful up removes the pointer immediately");
    expect(!state.touchAction(0, Phase::None), "idle frames after up do not repeatedly cancel a released pointer");

    state.markTouchSent(0, Action::Down);
    state.markTouchSent(1, Action::Down);
    expect(state.activeTouches().size() == 2, "multiple fingers have separate lifetimes");
    expect(state.touchAction(0, Phase::None) == Action::Cancel, "lost terminal event is cancelled only for an active pointer");
    state.markTouchSent(0, Action::Cancel);
    expect(!state.touchAction(0, Phase::None) && state.activeTouches().count(1) == 1,
           "cancelling one pointer does not affect another finger");
    state.markTouchSent(1, Action::Cancel);
    expect(!state.touchAction(1, Phase::Move), "held finger after overlay cancellation cannot resume without a new down");
    expect(state.touchAction(1, Phase::Start) == Action::Down, "new press after overlay is accepted");
    state.markTouchSent(1, Action::Down);
    state.begin(3);
    expect(state.activeTouches().empty() && !state.touchAction(1, Phase::Move), "reconnect forgets all old touches without sending old-session releases");
    state.markTouchSent(1, Action::Down);
    state.end();
    expect(!state.ready() && state.activeTouches().empty() && state.announcedMask() == 0,
           "ending a session discards controller and touch state");
    expect(!state.touchAction(1, Phase::Start), "touch events remain blocked after termination");
    std::cout << "Passed " << checks << " input lifecycle checks\n";
}
