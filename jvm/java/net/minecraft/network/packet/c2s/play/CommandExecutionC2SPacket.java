package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;

/** Minimal 1.21.4 command-execution packet ABI used by server mixins. */
public class CommandExecutionC2SPacket implements Packet<Object> {
    private final String command;
    public CommandExecutionC2SPacket() { this(""); }
    public CommandExecutionC2SPacket(String command) {
        this.command = command == null ? "" : command;
    }
    public String command() { return command; }
    public String getCommand() { return command; }
    @Override public void apply(Object listener) { }
}
