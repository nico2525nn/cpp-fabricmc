package net.minecraft.text;

import java.util.Objects;

/** Hover action attached to a text style. */
public final class HoverEvent {
    private final Action action;
    private final Object contents;

    public HoverEvent(Action action, Object contents) {
        this.action = action;
        this.contents = contents;
    }
    public Action getAction() { return action; }
    public Object getValue(Action requestedAction) {
        return action == requestedAction ? contents : null;
    }
    public Object getContents() { return contents; }
    @Override public boolean equals(Object other) {
        return other instanceof HoverEvent event && action == event.action && Objects.equals(contents, event.contents);
    }
    @Override public int hashCode() { return Objects.hash(action, contents); }

    public static final class Action {
        public static final Action SHOW_TEXT = new Action("show_text", true);
        public static final Action SHOW_ITEM = new Action("show_item", true);
        public static final Action SHOW_ENTITY = new Action("show_entity", true);
        private final String name;
        private final boolean parsable;
        private Action(String name, boolean parsable) { this.name = name; this.parsable = parsable; }
        public String getName() { return name; }
        public boolean isParsable() { return parsable; }
        public Object cast(Object value) { return value; }
        @Override public String toString() { return name; }
    }
}
