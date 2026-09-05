package net.minecraft.text;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.function.UnaryOperator;

/** Value-oriented text tree with the common MutableText construction API. */
public class Text {
    protected final TextContent content;
    protected Style style;
    protected final List<Text> siblings = new ArrayList<>();
    protected Text(TextContent content, Style style) { this.content = content == null ? new PlainTextContent("") : content; this.style = style == null ? Style.EMPTY : style; }
    public static MutableText literal(String value) { return new MutableText(new PlainTextContent(value), Style.EMPTY); }
    public static MutableText of(String value) { return literal(value); }
    public static MutableText empty() { return literal(""); }
    public static MutableText translatable(String key, Object... args) {
        String value = args == null || args.length == 0 ? key : key + " " + Arrays.toString(args);
        return new MutableText(new PlainTextContent(value), Style.EMPTY);
    }
    public String getString() { StringBuilder result = new StringBuilder(content.asString()); for (Text sibling : siblings) result.append(sibling.getString()); return result.toString(); }
    public String getLiteralString() { return content instanceof PlainTextContent ? content.asString() : null; }
    public TextContent getContent() { return content; }
    public List<Text> getSiblings() { return List.copyOf(siblings); }
    public Style getStyle() { return style; }
    public MutableText copy() { MutableText copy = new MutableText(content, style); copy.siblings.addAll(siblings); return copy; }
    public MutableText copyContentOnly() { return new MutableText(content, style); }
    public MutableText append(Text text) { return copy().append(text); }
    public MutableText append(String text) { return append(literal(text)); }
    public MutableText styled(UnaryOperator<Style> operator) { return copy().styled(operator); }
    public MutableText formatted(Formatting... formats) { return copy().formatted(formats); }
    public MutableText formatted(net.minecraft.util.Formatting... formats) { return copy().formatted(formats); }
    protected void appendInternal(Text text) { if (text != null) siblings.add(text); }
    protected void applyStyle(UnaryOperator<Style> operator) { if (operator != null) style = operator.apply(style); }
    protected void applyFormats(Formatting... formats) { if (formats != null) for (Formatting format : formats) if (format != null) style = style.withColor(format); }
    protected void applyCanonicalFormats(net.minecraft.util.Formatting... formats) {
        if (formats != null) for (net.minecraft.util.Formatting format : formats)
            if (format != null) style = style.withColor(format);
    }
    @Override public String toString() { return getString(); }
}
