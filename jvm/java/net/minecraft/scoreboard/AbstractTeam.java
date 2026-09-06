package net.minecraft.scoreboard;

/** Base scoreboard team API. */
public abstract class AbstractTeam {
    public enum CollisionRule { ALWAYS, NEVER, HIDE_FOR_OTHER_TEAMS, HIDE_FOR_OWN_TEAM }
    public enum VisibilityRule { ALWAYS, NEVER, HIDE_FOR_OTHER_TEAMS, HIDE_FOR_OWN_TEAM }
}
