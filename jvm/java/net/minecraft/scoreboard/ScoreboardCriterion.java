package net.minecraft.scoreboard;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;

/** Scoreboard criterion registry value. */
public class ScoreboardCriterion {
    public enum RenderType { INTEGER, HEARTS }
    public static final ScoreboardCriterion DUMMY = new ScoreboardCriterion("dummy");
    public static final Map<String, ScoreboardCriterion> CRITERIA = new LinkedHashMap<>();
    public static final Map<String, ScoreboardCriterion> SIMPLE_CRITERIA = CRITERIA;
    private final String name;
    private final boolean readOnly;
    private final RenderType defaultRenderType;

    public ScoreboardCriterion(String name) { this(name, false, RenderType.INTEGER); }
    public ScoreboardCriterion(String name, boolean readOnly, RenderType renderType) {
        this.name = name == null ? "" : name;
        this.readOnly = readOnly;
        this.defaultRenderType = renderType == null ? RenderType.INTEGER : renderType;
        CRITERIA.putIfAbsent(this.name, this);
    }
    public String getName() { return name; }
    public boolean isReadOnly() { return readOnly; }
    public RenderType getDefaultRenderType() { return defaultRenderType; }
    public static ScoreboardCriterion create(String name) { return new ScoreboardCriterion(name); }
    public static ScoreboardCriterion create(String name, boolean readOnly, RenderType type) {
        return new ScoreboardCriterion(name, readOnly, type);
    }
    public static Optional<ScoreboardCriterion> getOrCreateStatCriterion(String name) {
        return Optional.ofNullable(CRITERIA.get(name));
    }
}
