package net.minecraft.text;

import java.util.Objects;

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
    @Override public boolean equals(Object other) { return other instanceof Style s && formatting == s.formatting && Objects.equals(color, s.color) && Objects.equals(insertion, s.insertion) && bold == s.bold && italic == s.italic && underlined == s.underlined && strikethrough == s.strikethrough; }
    @Override public int hashCode() { return Objects.hash(formatting, color, insertion, bold, italic, underlined, strikethrough); }
}
