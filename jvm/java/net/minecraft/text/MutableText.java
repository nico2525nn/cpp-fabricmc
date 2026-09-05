package net.minecraft.text;

public class MutableText extends Text {
    protected MutableText(TextContent content, Style style) { super(content, style); }
    public MutableText append(Text text) { super.appendInternal(text); return this; }
    public MutableText append(String text) { return append(Text.literal(text)); }
    public MutableText styled(java.util.function.UnaryOperator<Style> operator) { super.applyStyle(operator); return this; }
    public MutableText formatted(Formatting... formats) { super.applyFormats(formats); return this; }
    public MutableText formatted(net.minecraft.util.Formatting... formats) {
        super.applyCanonicalFormats(formats); return this;
    }
    @Override public MutableText copy() { MutableText copy = new MutableText(content, style); copy.siblings.addAll(siblings); return copy; }
}
