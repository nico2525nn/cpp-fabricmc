package net.minecraft.state.property;

import java.util.List;
import java.util.Locale;
import java.util.Optional;

public final class BooleanProperty extends Property<Boolean> {
    private BooleanProperty(String name) { super(name); }
    public static BooleanProperty of(String name) { return new BooleanProperty(name); }
    @Override public java.util.Collection<Boolean> getValues() { return List.of(false, true); }
    @Override public Optional<Boolean> parse(String value) { return value == null ? Optional.empty() : switch (value.toLowerCase(Locale.ROOT)) { case "true" -> Optional.of(true); case "false" -> Optional.of(false); default -> Optional.empty(); }; }
}
