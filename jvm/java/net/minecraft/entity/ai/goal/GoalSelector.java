package net.minecraft.entity.ai.goal;

/** Lightweight goal scheduler surface exposed to server-side mixins. */
public class GoalSelector {
    public GoalSelector() { }
    public void add(int priority, Goal goal) { }
    public void remove(Goal goal) { }
    public void clear() { }
}
