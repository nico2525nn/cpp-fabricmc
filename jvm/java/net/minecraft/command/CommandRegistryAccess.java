package net.minecraft.command;

/** Canonical command registry access marker used during Fabric command setup. */
public class CommandRegistryAccess {
    public CommandRegistryAccess() {}
    public static CommandRegistryAccess of() { return new CommandRegistryAccess(); }
}
