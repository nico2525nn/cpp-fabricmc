package net.minecraft.state.property;

import java.util.Arrays;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.Optional;
import java.util.Set;
import java.util.function.Predicate;
import net.minecraft.util.math.Direction;

/** A block property backed by the named directions used by vanilla states. */
public final class DirectionProperty extends Property<Direction> {
    private final Set<Direction> values;

    private DirectionProperty(String name, Collection<Direction> values) {
        super(name);
        this.values = Set.copyOf(new LinkedHashSet<>(values));
    }

    public static DirectionProperty of(String name) {
        return of(name, direction -> true);
    }

    public static DirectionProperty of(String name, Predicate<Direction> predicate) {
        return new DirectionProperty(name, Arrays.stream(Direction.values()).filter(predicate).toList());
    }

    @Override public Collection<Direction> getValues() { return values; }
    @Override public String name(Direction value) { return value == null ? "" : value.name().toLowerCase(Locale.ROOT); }
    @Override public Optional<Direction> parse(String value) {
        if (value == null) return Optional.empty();
        String normalized = value.toLowerCase(Locale.ROOT);
        for (Direction direction : values) if (direction.name().toLowerCase(Locale.ROOT).equals(normalized)) return Optional.of(direction);
        return Optional.empty();
    }
}
