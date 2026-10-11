package cppfm.transformer_fixture;

public class SelectorTarget {
    public int run() { return marker(); }

    public int run(int value) { return marker() + value - 2; }

    public static int marker() { return 3; }
}
