#pragma once

namespace cppfm::secure_chat_policy {

enum class MessageDisposition {
    AcceptSigned,
    AcceptUnsigned,
    Reject,
    DropSessionUnsigned,
};

// Vanilla's secure-profile requirement also controls the secure-chat contract
// advertised in Join Game. cppfm's explicit secure-chat option may strengthen
// that contract, but must never weaken vanilla enforcement.
constexpr bool isEnforced(bool enforceSecureProfile,
                          bool cppfmEnforcesSecureChat) noexcept {
    return enforceSecureProfile || cppfmEnforcesSecureChat;
}

constexpr MessageDisposition classifyMessage(bool enforceSecureProfile,
                                               bool cppfmEnforcesSecureChat,
                                               bool hasSession,
                                               bool hasSignature,
                                               bool signatureValid) noexcept {
    if (hasSignature) {
        // A signature without a session, or a signature that failed
        // verification, is malformed/untrusted regardless of enforcement.
        return hasSession && signatureValid ? MessageDisposition::AcceptSigned
                                            : MessageDisposition::Reject;
    }
    if (isEnforced(enforceSecureProfile, cppfmEnforcesSecureChat))
        return MessageDisposition::Reject;

    // Verified vanilla evidence establishes unsigned acceptance before any
    // session update. Preserve cppfm's existing behavior for an unsigned
    // packet after a session update until that case is separately verified.
    return hasSession ? MessageDisposition::DropSessionUnsigned
                      : MessageDisposition::AcceptUnsigned;
}

} // namespace cppfm::secure_chat_policy
