package net.minecraft.entity.ai.goal;

/** Base goal ABI. */
public abstract class Goal {
    public enum Control { MOVE, LOOK, JUMP, TARGET }
    public boolean canStart() { return false; }
    public boolean shouldContinue() { return false; }
    public boolean canStop() { return true; }
    public void start() { }
    public void stop() { }
    public void tick() { }
}
