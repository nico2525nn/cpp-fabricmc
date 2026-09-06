package net.minecraft.text;

import java.util.ArrayList;
import java.util.List;
import java.util.function.UnaryOperator;

/** Mutable text contract matching the named 1.21.4 API. */
public class MutableText implements Text {
    private final TextContent content;
    private Style style;
    private final List<Text> siblings = new ArrayList<>();

    public MutableText(TextContent content, Style style) {
        this.content = content == null ? new PlainTextContent("") : content;
        this.style = style == null ? Style.EMPTY : style;
    }

    public static MutableText of(TextContent content) { return new MutableText(content, Style.EMPTY); }
    @Override public String getString() {
        StringBuilder result = new StringBuilder(content.asString());
        for (Text sibling : siblings) result.append(sibling.getString());
        return result.toString();
    }
    @Override public String getLiteralString() {
        return content instanceof PlainTextContent && siblings.isEmpty() ? content.asString() : null;
    }
    @Override public TextContent getContent() { return content; }
    @Override public List<Text> getSiblings() { return List.copyOf(siblings); }
    @Override public Style getStyle() { return style; }
    @Override public MutableText copy() {
        MutableText copy = new MutableText(content, style);
        copy.siblings.addAll(siblings);
        return copy;
    }
    @Override public MutableText copyContentOnly() { return new MutableText(content, style); }
    public MutableText append(Text text) { if (text != null) siblings.add(text); return this; }
    public MutableText append(String text) { return append(Text.literal(text)); }
    public MutableText setStyle(Style value) { style = value == null ? Style.EMPTY : value; return this; }
    /** Intermediary aliases retained for mods compiled against Yarn names. */
    public MutableText method_10852(Text text) { return append(text); }
    public MutableText method_10862(Style value) { return setStyle(value); }
    public Style method_10866() { return getStyle(); }
    public MutableText method_54663(int color) { return withColor(color); }
    public MutableText method_27693(String text) { return append(text); }
    public MutableText method_27694(UnaryOperator<Style> operator) { return styled(operator); }
    public MutableText method_27692(net.minecraft.util.Formatting format) { return formatted(format); }
    public MutableText method_27696(Style override) {
        return setStyle(getStyle() == Style.EMPTY ? override : getStyle());
    }
    public MutableText method_27695(net.minecraft.util.Formatting[] formats) { return formatted(formats); }
    public MutableText withColor(int color) { return setStyle(getStyle().withColor(color)); }
    public MutableText styled(UnaryOperator<Style> operator) {
        return setStyle(operator == null ? getStyle() : operator.apply(getStyle()));
    }
    public MutableText formatted(Formatting... formats) {
        if (formats != null) for (Formatting format : formats)
            if (format != null) setStyle(getStyle().withColor(format));
        return this;
    }
    public MutableText formatted(net.minecraft.util.Formatting... formats) {
        if (formats != null) for (net.minecraft.util.Formatting format : formats)
            if (format != null) setStyle(getStyle().withColor(format));
        return this;
    }
    @Override public String toString() { return getString(); }
}
