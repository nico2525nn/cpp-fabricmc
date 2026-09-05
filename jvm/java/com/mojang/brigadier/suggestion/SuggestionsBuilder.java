package com.mojang.brigadier.suggestion;

public class SuggestionsBuilder {
    private final String input;
    private final int start;
    public SuggestionsBuilder(String input, int start) {
        this.input = input == null ? "" : input; this.start = start;
    }
    public String getInput() { return input; }
    public int getStart() { return start; }
    public SuggestionsBuilder suggest(String value) { return this; }
    public Suggestions build() { return Suggestions.EMPTY; }
}
