// havel::ShouldRetrySelectInput - the rule that decides whether the X11
// adapter re-requests the root window event subscription.
//
// Background: the X server refuses XSelectInput on the root window with
// BadAccess (code 10) while another client holds an active keyboard or
// pointer grab. The ChangeWindowAttributes request would be delivered to
// the grabbing client instead, so the server refuses it.
//
// The refusal means the client receives NO key events on the root window -
// the X11 hotkey path goes completely deaf. The cause is transient (the
// other client eventually lets go), but before this rule existed the
// adapter asked once, in Init(), and never again: one refusal at startup
// left the process unable to receive any key event for its whole lifetime.
//
// The retry has to happen, but not on every poll round: the poll loop runs
// every millisecond and the X server would answer every one of those
// requests with BadAccess.
//
// Pure function: no X server, no display, no timing side effects.
// gtest declares a helper named Bool(); <X11/Xlib.h> defines Bool as a
// macro. gtest must come first so its declaration is not rewritten by the
// macro.
#include <gtest/gtest.h>

#include "core/io/X11Adapter.hpp"

using havel::ShouldRetrySelectInput;

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point At(int secondsFromEpoch) {
  return Clock::time_point(std::chrono::seconds(secondsFromEpoch));
}

} // namespace

TEST(ShouldRetrySelectInput, RefusedSubscriptionIsRetried) {
  // The subscription was refused, the display is live, and the retry
  // deadline has passed: ask again.
  EXPECT_TRUE(ShouldRetrySelectInput(/*subscribed=*/false, /*hasDisplay=*/true,
                                     At(10), At(9)));
}

TEST(ShouldRetrySelectInput, SubscribedClientDoesNotRetry) {
  // The subscription succeeded: asking again is pointless traffic.
  EXPECT_FALSE(ShouldRetrySelectInput(/*subscribed=*/true, /*hasDisplay=*/true,
                                      At(10), At(9)));
}

TEST(ShouldRetrySelectInput, NoDisplayMeansNoRetry) {
  // Without a connection there is nothing to ask on.
  EXPECT_FALSE(ShouldRetrySelectInput(/*subscribed=*/false, /*hasDisplay=*/false,
                                      At(10), At(9)));
}

TEST(ShouldRetrySelectInput, RetryIsRateLimited) {
  // The poll loop runs every millisecond. The X server would answer every
  // one of those requests with BadAccess, so the retry waits for its
  // deadline.
  EXPECT_FALSE(ShouldRetrySelectInput(/*subscribed=*/false, /*hasDisplay=*/true,
                                      At(10), At(11)));
}

TEST(ShouldRetrySelectInput, RetryHappensExactlyAtTheDeadline) {
  EXPECT_TRUE(ShouldRetrySelectInput(/*subscribed=*/false, /*hasDisplay=*/true,
                                     At(10), At(10)));
}

TEST(ShouldRetrySelectInput, RefusedSubscriptionRecoversAfterTheDeadline) {
  // The whole point: the obstruction is transient, so the retry must fire
  // once the other client let go.
  const Clock::time_point refusedAt = At(100);
  const Clock::time_point deadline = refusedAt + std::chrono::seconds(1);

  // Immediately after the refusal: too early.
  EXPECT_FALSE(ShouldRetrySelectInput(false, true, refusedAt, deadline));
  // A second later: the retry fires.
  EXPECT_TRUE(ShouldRetrySelectInput(false, true, deadline, deadline));
}
