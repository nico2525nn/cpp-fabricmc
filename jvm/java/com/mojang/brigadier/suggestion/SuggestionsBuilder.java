package com.mojang.brigadier.suggestion;

import com.mojang.brigadier.context.StringRange;
import java.util.ArrayList;
import java.util.List;

public class SuggestionsBuilder {
    private final String input;
    private final int start;
    private final List<Suggestion> suggestions = new ArrayList<>();
    public SuggestionsBuilder(String input, int start) { this.input = input == null ? "" : input; this.start = Math.max(0, Math.min(start, this.input.length())); }
    public String getInput() { return input; }
    public int getStart() { return start; }
    public SuggestionsBuilder suggest(String value) { return suggest(value, null); }
    public SuggestionsBuilder suggest(String value, String tooltip) { if (value != null) suggestions.add(new Suggestion(StringRange.between(start, input.length()), value, tooltip)); return this; }
    public SuggestionsBuilder createOffset(int offset) { return new SuggestionsBuilder(input, start + offset); }
    public SuggestionsBuilder restart() { return new SuggestionsBuilder(input, start); }
    public Suggestions build() { return new Suggestions(StringRange.between(start, input.length()), suggestions); }
    public java.util.concurrent.CompletableFuture<Suggestions> buildFuture() { return java.util.concurrent.CompletableFuture.completedFuture(build()); }
}
