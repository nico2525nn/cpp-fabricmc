package net.fabricmc.fabric.api.gamerule.v1.rule;

/** A custom game rule that can validate its serialized value. */
public interface ValidateableRule {
    boolean validate(String input);
}
