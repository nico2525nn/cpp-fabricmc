package net.minecraft.scoreboard;

import net.minecraft.text.Text;

/** Named scoreboard objective. */
public class ScoreboardObjective {
    private final Scoreboard scoreboard;
    private final String name;
    private final ScoreboardCriterion criterion;
    private Text displayName;
    private ScoreboardCriterion.RenderType renderType;

    public ScoreboardObjective(Scoreboard scoreboard, String name, ScoreboardCriterion criterion,
                               Text displayName, ScoreboardCriterion.RenderType renderType,
                               boolean displayAutoUpdate, Object numberFormat) {
        this.scoreboard = scoreboard;
        this.name = name == null ? "" : name;
        this.criterion = criterion == null ? ScoreboardCriterion.DUMMY : criterion;
        this.displayName = displayName == null ? Text.literal(this.name) : displayName;
        this.renderType = renderType == null ? ScoreboardCriterion.RenderType.INTEGER : renderType;
    }
    public ScoreboardObjective(Scoreboard scoreboard, String name, ScoreboardCriterion criterion,
                               Text displayName, ScoreboardCriterion.RenderType renderType) {
        this(scoreboard, name, criterion, displayName, renderType, false, null);
    }
    public Scoreboard getScoreboard() { return scoreboard; }
    public String getName() { return name; }
    public ScoreboardCriterion getCriterion() { return criterion; }
    public Text getDisplayName() { return displayName; }
    public void setDisplayName(Text value) { displayName = value == null ? Text.empty() : value; }
    public ScoreboardCriterion.RenderType getRenderType() { return renderType; }
    public void setRenderType(ScoreboardCriterion.RenderType value) { renderType = value; }
    public boolean shouldDisplayAutoUpdate() { return false; }
    public Text toHoverableText() { return displayName; }
}
