package net.minecraft.command;

import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.Predicate;
import net.minecraft.text.Text;

/** Registry-backed custom selector option table used by command parsing. */
public final class EntitySelectorOptions {
    private static final Map<String, SelectorOption> OPTIONS = new LinkedHashMap<>();

    @FunctionalInterface
    public interface SelectorHandler {
        void handle(EntitySelectorReader reader) throws CommandSyntaxException;
    }

    public static final class SelectorOption {
        private final SelectorHandler handler;
        private final Predicate<EntitySelectorReader> condition;
        private final Text description;

        private SelectorOption(SelectorHandler handler, Predicate<EntitySelectorReader> condition,
                               Text description) {
            this.handler = handler; this.condition = condition; this.description = description;
        }
        public SelectorHandler handler() { return handler; }
        public Predicate<EntitySelectorReader> condition() { return condition; }
        public Text description() { return description; }
    }

    private EntitySelectorOptions() { }

    public static synchronized void putOption(String id, SelectorHandler handler,
                                               Predicate<EntitySelectorReader> condition,
                                               Text description) {
        if (id == null || handler == null || condition == null)
            throw new NullPointerException("id/handler/condition");
        OPTIONS.put(id, new SelectorOption(handler, condition, description));
    }

    public static synchronized SelectorHandler getHandler(EntitySelectorReader reader,
                                                           String option, int restoreCursor)
            throws CommandSyntaxException {
        SelectorOption value = OPTIONS.get(option);
        if (value == null) throw new CommandSyntaxException("Unknown option: " + option);
        if (reader == null || !value.condition().test(reader))
            throw new CommandSyntaxException("Option cannot be applied: " + option);
        return value.handler();
    }

    public static synchronized Map<String, SelectorOption> getOptions() {
        return java.util.Collections.unmodifiableMap(new LinkedHashMap<>(OPTIONS));
    }
}
