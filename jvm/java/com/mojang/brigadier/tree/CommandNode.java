package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.Predicate;

/** Lightweight command-tree node used by the embedded compatibility layer. */
public abstract class CommandNode<S> {
    protected final Command<S> command;
    protected final Predicate<S> requirement;
    protected final Map<String, CommandNode<S>> children = new LinkedHashMap<>();

    protected CommandNode(Command<S> command, Predicate<S> requirement) {
        this.command = command;
        this.requirement = requirement == null ? source -> true : requirement;
    }
    public Command<S> getCommand() { return command; }
    public Collection<CommandNode<S>> getChildren() { return java.util.List.copyOf(children.values()); }
    public CommandNode<S> getChild(String name) { return children.get(name); }
    public void addChild(CommandNode<S> node) { if (node != null) children.put(node.getName(), node); }
    public boolean canUse(S source) { return requirement.test(source); }
    public CommandNode<S> getRedirect() { return null; }
    public boolean isFork() { return false; }
    public abstract String getName();
    public String getUsageText() { return getName(); }
}
