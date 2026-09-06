package net.minecraft.util.context;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Objects;
import java.util.Set;

/** Describes which context parameters are required or permitted. */
public final class ContextType {
    private final Set<ContextParameter<?>> required;
    private final Set<ContextParameter<?>> allowed;

    public ContextType(Set<ContextParameter<?>> required, Set<ContextParameter<?>> allowed) {
        this.required = immutable(required);
        this.allowed = immutable(allowed);
    }

    private static Set<ContextParameter<?>> immutable(Set<ContextParameter<?>> values) {
        return Collections.unmodifiableSet(new LinkedHashSet<>(values == null ? Set.of() : values));
    }

    public Set<ContextParameter<?>> getRequired() { return required; }
    public Set<ContextParameter<?>> getAllowed() { return allowed; }

    /** The named 1.21.4 method used to describe an invalid parameter. */
    public String method_779(ContextParameter<?> parameter) {
        return parameter == null ? "null" : parameter.getId().toString();
    }

    public static Builder builder() { return new Builder(); }

    public static final class Builder {
        private final Set<ContextParameter<?>> required = new LinkedHashSet<>();
        private final Set<ContextParameter<?>> allowed = new LinkedHashSet<>();

        public Builder require(ContextParameter<?> parameter) {
            required.add(Objects.requireNonNull(parameter, "parameter"));
            allowed.add(parameter);
            return this;
        }

        public Builder allow(ContextParameter<?> parameter) {
            allowed.add(Objects.requireNonNull(parameter, "parameter"));
            return this;
        }

        public ContextType build() { return new ContextType(required, allowed); }
    }
}
