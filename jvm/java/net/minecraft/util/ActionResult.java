package net.minecraft.util;

public enum ActionResult {
    PASS, SUCCESS, FAIL, CONSUME, SUCCESS_SERVER, SUCCESS_CLIENT;

    public boolean isAccepted() { return this == SUCCESS || this == CONSUME || this == SUCCESS_SERVER || this == SUCCESS_CLIENT; }
}
