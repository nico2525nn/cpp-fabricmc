package net.minecraft.util;

/** Server/client interaction result with 1.21-style acceptance semantics. */
public enum ActionResult {
    PASS,
    SUCCESS,
    FAIL,
    CONSUME,
    SUCCESS_SERVER,
    SUCCESS_CLIENT;

    public boolean isAccepted() {
        return this == SUCCESS || this == CONSUME || this == SUCCESS_SERVER || this == SUCCESS_CLIENT;
    }
    public boolean shouldSwingHand() { return isAccepted() && this != CONSUME; }
    public boolean isAccepted(boolean client) {
        return this == SUCCESS || this == CONSUME || (client ? this == SUCCESS_CLIENT : this == SUCCESS_SERVER);
    }
}
