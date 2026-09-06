package net.minecraft.scoreboard;

import java.util.LinkedHashMap;
import java.util.Map;

/** Per-holder objective scores. */
public class Scores {
    private final Map<ScoreboardObjective, ScoreboardScore> scores = new LinkedHashMap<>();
    public Map<ScoreboardObjective, ScoreboardScore> getScores() { return Map.copyOf(scores); }
    public ScoreboardScore getOrCreate(ScoreboardObjective objective) {
        return scores.computeIfAbsent(objective, key -> new ScoreboardScore());
    }
}
