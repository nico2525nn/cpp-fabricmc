package net.minecraft.scoreboard;

import net.minecraft.text.Text;

/** Immutable entry returned by scoreboard queries. */
public record ScoreboardEntry(String owner, int value, Text display,
                              Object numberFormatOverride) {
    public boolean hidden() { return false; }
    public Text name() { return display == null ? Text.literal(owner) : display; }
    public Text formatted(Object format) { return name(); }
}
