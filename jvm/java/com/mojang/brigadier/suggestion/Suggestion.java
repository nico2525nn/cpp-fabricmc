package com.mojang.brigadier.suggestion;

import com.mojang.brigadier.context.StringRange;

public final class Suggestion implements Comparable<Suggestion> {
    private final StringRange range;
    private final String text;
    private final String tooltip;
    public Suggestion(StringRange range, String text) { this(range, text, null); }
    public Suggestion(StringRange range, String text, String tooltip) { this.range = range; this.text = text == null ? "" : text; this.tooltip = tooltip; }
    public StringRange getRange() { return range; }
    public String getText() { return text; }
    public String getTooltip() { return tooltip; }
    public String apply(String input) { return input.substring(0, range.getStart()) + text + input.substring(range.getEnd()); }
    @Override public int compareTo(Suggestion other) { return text.compareTo(other == null ? "" : other.text); }
    @Override public boolean equals(Object other) { return other instanceof Suggestion value && text.equals(value.text) && range.getStart() == value.range.getStart() && range.getEnd() == value.range.getEnd(); }
    @Override public int hashCode() { return java.util.Objects.hash(range.getStart(), range.getEnd(), text); }
}
