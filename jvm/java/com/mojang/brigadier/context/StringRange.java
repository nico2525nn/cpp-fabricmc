package com.mojang.brigadier.context;

public final class StringRange {
    private final int start, end;
    private StringRange(int start, int end) { this.start = start; this.end = end; }
    public static StringRange between(int start, int end) { return new StringRange(Math.min(start, end), Math.max(start, end)); }
    public static StringRange at(int position) { return new StringRange(position, position); }
    public static StringRange encompassing(StringRange first, StringRange second) { return between(Math.min(first.start, second.start), Math.max(first.end, second.end)); }
    public int getStart() { return start; }
    public int getEnd() { return end; }
    public int getLength() { return end - start; }
    public boolean isEmpty() { return start == end; }
    @Override public String toString() { return "StringRange{" + start + "," + end + "}"; }
}
