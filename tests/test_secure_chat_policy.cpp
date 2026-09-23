#include "game/SecureChatPolicy.hpp"

#include <iostream>

using cppfm::secure_chat_policy::MessageDisposition;

namespace {
int passed = 0;
int failed = 0;

void check(bool condition, const char* description) {
    if (condition) {
        ++passed;
        std::cout << "PASS " << description << '\n';
    } else {
        ++failed;
        std::cout << "FAIL " << description << '\n';
    }
}
}

int main() {
    using cppfm::secure_chat_policy::classifyMessage;
    using cppfm::secure_chat_policy::isEnforced;

    check(!isEnforced(false, false),
          "join secure-chat flag is false when both enforcement options are false");
    check(isEnforced(true, false),
          "vanilla secure-profile enforcement sets the join secure-chat flag");
    check(isEnforced(false, true),
          "cppfm secure-chat extension independently sets the join flag");
    check(isEnforced(true, true),
          "cppfm extension cannot weaken vanilla secure-profile enforcement");

    check(classifyMessage(false, false, false, false, false) ==
              MessageDisposition::AcceptUnsigned,
          "no-session unsigned chat is accepted when secure chat is unsecure");
    check(classifyMessage(true, false, false, false, false) ==
              MessageDisposition::Reject,
          "secure-profile enforcement rejects no-session unsigned chat");
    check(classifyMessage(false, true, false, false, false) ==
              MessageDisposition::Reject,
          "explicit cppfm secure-chat enforcement rejects unsigned chat");

    check(classifyMessage(false, false, true, true, true) ==
              MessageDisposition::AcceptSigned,
          "a verified signed message with a session is accepted");
    check(classifyMessage(false, false, true, true, false) ==
              MessageDisposition::Reject,
          "an invalid signed message fails closed without enforcement");
    check(classifyMessage(false, false, false, true, false) ==
              MessageDisposition::Reject,
          "a signed message without a player session fails closed");

    check(classifyMessage(false, false, true, false, false) ==
              MessageDisposition::DropSessionUnsigned,
          "session-present unsigned behavior remains unchanged pending vanilla verification");
    check(classifyMessage(true, false, true, false, false) ==
              MessageDisposition::Reject,
          "secure-profile enforcement rejects session-present unsigned chat");

    std::cout << "secure_chat_policy: " << passed << " PASS " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
