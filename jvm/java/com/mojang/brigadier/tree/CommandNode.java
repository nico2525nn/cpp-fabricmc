package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.RedirectModifier;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import java.util.function.Predicate;

/** Lightweight command-tree node used by the embedded compatibility layer. */
public abstract class CommandNode<S> {
    protected final Command<S> command;
    protected final Predicate<S> requirement;
    protected final Map<String, CommandNode<S>> children = new LinkedHashMap<>();
    protected final CommandNode<S> redirect;
    protected final RedirectModifier<S> redirectModifier;
    protected final boolean forks;

    protected CommandNode(Command<S> command, Predicate<S> requirement) {
        this(command, requirement, null, null, false);
    }
    protected CommandNode(Command<S> command, Predicate<S> requirement, CommandNode<S> redirect,
                          RedirectModifier<S> redirectModifier, boolean forks) {
        this.command = command;
        this.requirement = requirement == null ? source -> true : requirement;
        this.redirect = redirect; this.redirectModifier = redirectModifier; this.forks = forks;
    }
    public Command<S> getCommand() { return command; }
    public Collection<CommandNode<S>> getChildren() { return List.copyOf(children.values()); }
    public CommandNode<S> getChild(String name) { return children.get(name); }
    public void addChild(CommandNode<S> node) { if (node != null) children.put(node.getName(), node); }
    public boolean canUse(S source) { return requirement.test(source); }
    public Predicate<S> getRequirement() { return requirement; }
    public CommandNode<S> getRedirect() { return redirect; }
    public RedirectModifier<S> getRedirectModifier() { return redirectModifier; }
    public boolean isFork() { return forks; }
    public Collection<String> getExamples() { return List.of(); }
    public Collection<CommandNode<S>> getRelevantNodes(com.mojang.brigadier.StringReader input) { return getChildren(); }
    public void findAmbiguities(Object consumer) { }
    public void setCommand(Command<S> command) { }
    public abstract String getName();
    public String getUsageText() { return getName(); }
}
