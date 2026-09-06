package net.minecraft.scoreboard;

import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.List;
import it.unimi.dsi.fastutil.objects.Reference2ObjectMap;
import it.unimi.dsi.fastutil.objects.Reference2ObjectOpenHashMap;

/** Minimal deterministic scoreboard registry for JVM extensions. */
public class Scoreboard {
    private final Map<String, ScoreboardObjective> objectives = new LinkedHashMap<>();
    private final Reference2ObjectMap<ScoreboardCriterion, List<ScoreboardObjective>> objectivesByCriterion =
        new Reference2ObjectOpenHashMap<>();
    private final Map<String, Team> teams = new LinkedHashMap<>();
    private final Map<String, Scores> scores = new LinkedHashMap<>();

    public ScoreboardObjective getNullableObjective(String name) { return objectives.get(name); }
    public Collection<ScoreboardObjective> getObjectives() { return objectives.values(); }
    public ScoreboardObjective addObjective(String name, ScoreboardCriterion criterion) {
        return addObjective(name, criterion, null, ScoreboardCriterion.RenderType.INTEGER, false, null);
    }
    public ScoreboardObjective addObjective(String name, ScoreboardCriterion criterion, net.minecraft.text.Text displayName,
                                            ScoreboardCriterion.RenderType renderType, boolean displayAutoUpdate,
                                            Object numberFormat) {
        ScoreboardObjective objective = new ScoreboardObjective(this, name, criterion, displayName,
                renderType, displayAutoUpdate, numberFormat);
        objectives.put(name, objective);
        objectivesByCriterion.computeIfAbsent(objective.getCriterion(), ignored -> new java.util.ArrayList<>()).add(objective);
        return objective;
    }
    public void removeObjective(ScoreboardObjective objective) { if (objective != null) objectives.remove(objective.getName()); }
    public Team addTeam(String name) { return teams.computeIfAbsent(name, key -> new Team(this, key)); }
    public Team getTeam(String name) { return teams.get(name); }
    public Collection<String> getTeamNames() { return teams.keySet(); }
    public void removeTeam(Team team) { if (team != null) teams.remove(team.getName()); }
    public ScoreboardScore getOrCreateScore(ScoreHolder holder, ScoreboardObjective objective) {
        return scores.computeIfAbsent(holder.getNameForScoreboard(), key -> new Scores()).getOrCreate(objective);
    }
    public ScoreAccess getOrCreateScore(ScoreHolder holder, ScoreboardObjective objective, boolean forceWritable) {
        return getOrCreateScore(holder, objective);
    }
    public ReadableScoreboardScore getScore(ScoreHolder holder, ScoreboardObjective objective) {
        return getOrCreateScore(holder, objective);
    }
    public void resetScore(ScoreHolder holder, ScoreboardObjective objective) {
        Scores values = scores.get(holder.getNameForScoreboard());
        if (values != null) values.getScores().remove(objective);
    }
    public void removeScore(ScoreHolder holder, ScoreboardObjective objective) { resetScore(holder, objective); }
}
