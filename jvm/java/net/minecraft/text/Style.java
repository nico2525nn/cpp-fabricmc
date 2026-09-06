package net.minecraft.text;

import java.util.Objects;
import net.minecraft.util.Identifier;

public final class Style {
    public static final Style EMPTY = new Style(null, null, null, false, false, false, false);
    private final Formatting formatting;
    private final Integer color;
    private final String insertion;
    private final boolean bold, italic, underlined, strikethrough;
    private Style(Formatting formatting, Integer color, String insertion, boolean bold, boolean italic, boolean underlined, boolean strikethrough) {
        this.formatting = formatting; this.color = color; this.insertion = insertion; this.bold = bold; this.italic = italic; this.underlined = underlined; this.strikethrough = strikethrough;
    }
    public Style withColor(Formatting value) { return new Style(value, value == null ? null : value.getColorValue(), insertion, bold, italic, underlined, strikethrough); }
    public Style withColor(net.minecraft.util.Formatting value) {
        return new Style(null, value == null ? null : value.getColorValue(), insertion,
            bold, italic, underlined, strikethrough);
    }
    public Style withFormatting(Formatting value) { return withColor(value); }
    public Style withFormatting(net.minecraft.util.Formatting value) { return withColor(value); }
    public Style withFormatting(net.minecraft.util.Formatting... values) {
        Style result = this;
        if (values != null) for (net.minecraft.util.Formatting value : values)
            if (value != null) result = result.withFormatting(value);
        return result;
    }
    public Style withExclusiveFormatting(net.minecraft.util.Formatting value) { return withFormatting(value); }
    public Style withColor(Integer value) { return new Style(formatting, value, insertion, bold, italic, underlined, strikethrough); }
    public Style withBold(Boolean value) { return new Style(formatting, color, insertion, Boolean.TRUE.equals(value), italic, underlined, strikethrough); }
    public Style withItalic(Boolean value) { return new Style(formatting, color, insertion, bold, Boolean.TRUE.equals(value), underlined, strikethrough); }
    public Style withUnderline(Boolean value) { return new Style(formatting, color, insertion, bold, italic, Boolean.TRUE.equals(value), strikethrough); }
    public Style withStrikethrough(Boolean value) { return new Style(formatting, color, insertion, bold, italic, underlined, Boolean.TRUE.equals(value)); }
    public Style withInsertion(String value) { return new Style(formatting, color, value, bold, italic, underlined, strikethrough); }
    public Formatting getFormatting() { return formatting; }
    public Integer getColor() { return color; }
    public String getInsertion() { return insertion; }
    public Boolean isBold() { return bold; }
    public Boolean isItalic() { return italic; }
    public Boolean isUnderlined() { return underlined; }
    public Boolean isStrikethrough() { return strikethrough; }
    public Boolean isObfuscated() { return false; }
    public HoverEvent getHoverEvent() { return null; }
    public ClickEvent getClickEvent() { return null; }
    public Style withHoverEvent(HoverEvent event) { return this; }
    public Style withClickEvent(ClickEvent event) { return this; }
    public Style withFont(Identifier font) { return this; }
    public Style withParent(Style parent) { return this; }
    public HoverEvent method_10969() { return getHoverEvent(); }
    public boolean method_10966() { return Boolean.TRUE.equals(isItalic()); }
    public Style method_10958(ClickEvent event) { return withClickEvent(event); }
    public Style method_10949(HoverEvent event) { return withHoverEvent(event); }
    public Style method_10975(String value) { return withInsertion(value); }
    public boolean method_10984() { return Boolean.TRUE.equals(isBold()); }
    public boolean method_10986() { return Boolean.TRUE.equals(isStrikethrough()); }
    public boolean method_10965() { return Boolean.TRUE.equals(isUnderlined()); }
    public boolean method_10987() { return Boolean.TRUE.equals(isObfuscated()); }
    public boolean method_10967() { return this == EMPTY || (formatting == null && color == null && insertion == null && !bold && !italic && !underlined && !strikethrough); }
    public String method_10955() { return getInsertion(); }
    public Style method_27703(Object value) { return value instanceof net.minecraft.util.Formatting f ? withColor(f) : this; }
    public Style method_27704(Identifier font) { return withFont(font); }
    public Style method_27702(Style parent) { return withParent(parent); }
    public Style method_36139(int rgb) { return withColor(rgb); }
    public Style method_36141(Boolean value) { return new Style(formatting, color, insertion, bold, italic, underlined, strikethrough); }
    public Style method_30938(Boolean value) { return withUnderline(value); }
    public Integer method_65301() { return null; }
    public net.minecraft.util.Formatting method_10973() { return formatting == null ? null : net.minecraft.util.Formatting.byName(formatting.name()); }
    public Identifier method_27708() { return Identifier.ofVanilla("default"); }
    public Style method_27707(Object formatting) { return this; }
    public Style method_10978(Boolean value) { return withItalic(value); }
    public Style method_10982(Boolean value) { return withBold(value); }
    public Style method_10977(net.minecraft.util.Formatting value) { return withColor(value); }
    public Style method_36140(Boolean value) { return withStrikethrough(value); }
    public Style method_65302(int value) { return this; }
    @Override public boolean equals(Object other) { return other instanceof Style s && formatting == s.formatting && Objects.equals(color, s.color) && Objects.equals(insertion, s.insertion) && bold == s.bold && italic == s.italic && underlined == s.underlined && strikethrough == s.strikethrough; }
    @Override public int hashCode() { return Objects.hash(formatting, color, insertion, bold, italic, underlined, strikethrough); }
}
