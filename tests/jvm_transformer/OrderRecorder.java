package cppfm.transformer_fixture;

public final class OrderRecorder {
    public static final StringBuilder LOG = new StringBuilder();

    private OrderRecorder() { }

    public static void reset() { LOG.setLength(0); }
}
