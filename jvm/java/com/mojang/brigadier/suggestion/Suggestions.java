package com.mojang.brigadier.suggestion;

import com.mojang.brigadier.context.StringRange;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class Suggestions {
    public static final Suggestions EMPTY = new Suggestions(StringRange.at(0), List.of());
    private final StringRange range;
    private final List<Suggestion> suggestions;
    public Suggestions(StringRange range, List<Suggestion> suggestions) { this.range = range; this.suggestions = List.copyOf(suggestions == null ? List.of() : suggestions); }
    public StringRange getRange() { return range; }
    public List<Suggestion> getList() { return suggestions; }
    public boolean isEmpty() { return suggestions.isEmpty(); }
    public static Suggestions merge(String command, List<Suggestions> values) {
        if (values == null || values.isEmpty()) return EMPTY;
        List<Suggestion> merged = new ArrayList<>(); for (Suggestions value : values) if (value != null) merged.addAll(value.suggestions);
        Collections.sort(merged); return new Suggestions(values.get(0).range, merged);
    }
    @Override public String toString() { return suggestions.toString(); }
}
