package net.minecraft.text;

import java.util.Objects;

/** Click action attached to a text style. */
public final class ClickEvent {
    private final Action action;
    private final String value;

    public ClickEvent(Action action, String value) {
        this.action = action;
        this.value = value == null ? "" : value;
    }

    public String getValue() { return value; }
    public Action getAction() { return action; }
    public String method_10844() { return getValue(); }
    public Action method_10845() { return getAction(); }

    @Override public boolean equals(Object other) {
        return other instanceof ClickEvent event && action == event.action && value.equals(event.value);
    }
    @Override public int hashCode() { return Objects.hash(action, value); }

    public static final class Action {
        public static final Action OPEN_URL = new Action("open_url", true);
        public static final Action OPEN_FILE = new Action("open_file", false);
        public static final Action RUN_COMMAND = new Action("run_command", true);
        public static final Action SUGGEST_COMMAND = new Action("suggest_command", true);
        public static final Action CHANGE_PAGE = new Action("change_page", true);
        public static final Action COPY_TO_CLIPBOARD = new Action("copy_to_clipboard", true);
        private final String name;
        private final boolean userDefinable;
        private Action(String name, boolean userDefinable) { this.name = name; this.userDefinable = userDefinable; }
        public String getName() { return name; }
        public boolean isUserDefinable() { return userDefinable; }
        @Override public String toString() { return name; }
    }
}
