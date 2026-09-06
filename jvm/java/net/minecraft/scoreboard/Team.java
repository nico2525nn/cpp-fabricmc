package net.minecraft.scoreboard;

import net.minecraft.text.Text;
import net.minecraft.util.Formatting;

/** Native-independent team value used by the shadow scoreboard API. */
public class Team extends AbstractTeam {
    private final Scoreboard scoreboard;
    private final String name;
    private Text displayName;
    private Text prefix = Text.empty();
    private Text suffix = Text.empty();
    private Formatting color = Formatting.RESET;

    public Team(Scoreboard scoreboard, String name) {
        this.scoreboard = scoreboard; this.name = name == null ? "" : name;
        this.displayName = Text.literal(this.name);
    }
    public Scoreboard getScoreboard() { return scoreboard; }
    public String getName() { return name; }
    public Text getDisplayName() { return displayName; }
    public void setDisplayName(Text value) { displayName = value == null ? Text.empty() : value; }
    public Text getPrefix() { return prefix; }
    public void setPrefix(Text value) { prefix = value == null ? Text.empty() : value; }
    public Text getSuffix() { return suffix; }
    public void setSuffix(Text value) { suffix = value == null ? Text.empty() : value; }
    public Formatting getColor() { return color; }
    public void setColor(Formatting value) { color = value == null ? Formatting.RESET : value; }
}
