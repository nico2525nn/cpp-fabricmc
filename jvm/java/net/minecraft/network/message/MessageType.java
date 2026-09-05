package net.minecraft.network.message;

import net.minecraft.text.Text;

/** Minimal signed-chat message type descriptor used by server callbacks. */
public final class MessageType {
    private final String id;
    public MessageType(String id) { this.id = id == null ? "chat" : id; }
    public String id() { return id; }
    public static final class Parameters {
        public static final Parameters EMPTY = new Parameters(Text.empty(), Text.empty());
        private final Text name;
        private final Text targetName;
        public Parameters(Text name, Text targetName) {
            this.name = name == null ? Text.empty() : name;
            this.targetName = targetName == null ? Text.empty() : targetName;
        }
        public Text name() { return name; }
        public Text targetName() { return targetName; }
        public Text getName() { return name; }
        public Text getTargetName() { return targetName; }
    }
}
