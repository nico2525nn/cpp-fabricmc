package net.minecraft.scoreboard;

import net.minecraft.text.MutableText;
import net.minecraft.text.Style;
import net.minecraft.text.Text;

/** Stable identity used by scoreboard objectives. */
public interface ScoreHolder {
    String WILDCARD_NAME = "*";
    ScoreHolder WILDCARD = fromName(WILDCARD_NAME);

    String getNameForScoreboard();
    default Text getDisplayName() { return Text.literal(getNameForScoreboard()); }
    default Text getStyledDisplayName() { return getDisplayName(); }
    default Style method_55421(Style style) { return style == null ? Style.EMPTY : style; }

    static ScoreHolder fromName(String name) {
        final String value = name == null ? "" : name;
        return new ScoreHolder() {
            @Override public String getNameForScoreboard() { return value; }
            @Override public Text getDisplayName() { return Text.literal(value); }
        };
    }

    /** Compatibility helper for callers that already have a display name. */
    static ScoreHolder fromText(Text text) {
        MutableText value = text == null ? Text.empty() : text.copy();
        return fromName(value.getString());
    }
}
