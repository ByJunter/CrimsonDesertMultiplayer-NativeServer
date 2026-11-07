// Only the mercenary clan's hide of another user's main character is refused.
#include <cstdio>

#include "mod/session2/remote_visible_logic.h"

using namespace cdmp::session2::rvis;

namespace {

constexpr uint32_t kHostCharacter = 0xA0100001, kGuestCharacter = 0xA0100066, kCompanion = 0xB0100002;
constexpr uintptr_t kThisClient = 0x1000, kOtherUser = 0x2000;

int g_failed = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++g_failed;
    }
}

}  // namespace

int main() {
    check(refuse_hide(kHiddenByClanBit, true, kHostCharacter, kOtherUser, kThisClient),
          "the guest's client keeps the host's character visible");
    check(refuse_hide(kHiddenByClanBit, true, kGuestCharacter, kOtherUser, kThisClient),
          "the host's client keeps the guest's character visible");
    check(!refuse_hide(kHiddenByClanBit, true, kHostCharacter, kThisClient, kThisClient),
          "this client's own character stays native");
    check(!refuse_hide(kHiddenByClanBit, true, kCompanion, kOtherUser, kThisClient),
          "a companion called off is still hidden");
    check(!refuse_hide(kHiddenByClanBit, false, kHostCharacter, kOtherUser, kThisClient),
          "clearing the bit is never refused");
    check(!refuse_hide(0x2, true, kHostCharacter, kOtherUser, kThisClient),
          "the login's own bit 2 still fades the model in");
    check(!refuse_hide(0x80, true, kHostCharacter, kOtherUser, kThisClient), "other reasons to hide stay native");
    check(!refuse_hide(kHiddenByClanBit, true, kHostCharacter, 0, kThisClient), "no user on the actor: native");
    check(!refuse_hide(kHiddenByClanBit, true, kHostCharacter, kOtherUser, 0), "local user not known yet: native");
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("remote visible: all checks passed\n");
    return 0;
}
