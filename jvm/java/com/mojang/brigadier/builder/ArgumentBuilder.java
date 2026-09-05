package com.mojang.brigadier.builder;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.RedirectModifier;
import com.mojang.brigadier.SingleRedirectModifier;
import com.mojang.brigadier.tree.CommandNode;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.function.Predicate;

@SuppressWarnings("unchecked")
public abstract class ArgumentBuilder<S, T extends ArgumentBuilder<S, T>> {
    protected final List<ArgumentBuilder<S, ?>> children = new ArrayList<>();
    protected Command<S> command;
    protected Predicate<S> requirement = source -> true;
    public T then(ArgumentBuilder<S, ?> child) { if (child != null) children.add(child); return (T) this; }
    public T then(CommandNode<S> child) { return (T) this; }
    public T executes(Command<S> command) { this.command = command; return (T) this; }
    public List<ArgumentBuilder<S, ?>> getChildren() { return List.copyOf(children); }
    public Collection<CommandNode<S>> getArguments() {
        List<CommandNode<S>> result = new ArrayList<>();
        for (ArgumentBuilder<S, ?> child : children) result.add(child.build());
        return List.copyOf(result);
    }
    public Command<S> getCommand() { return command; }
    public T requires(Predicate<S> requirement) { this.requirement = requirement == null ? source -> true : requirement; return (T) this; }
    public Predicate<S> getRequirement() { return requirement; }
    public T redirect(CommandNode<S> target) { return (T) this; }
    public T redirect(CommandNode<S> target, SingleRedirectModifier<S> modifier) { return (T) this; }
    public T fork(CommandNode<S> target, RedirectModifier<S> modifier) { return (T) this; }
    public CommandNode<S> build() { return null; }
    public abstract String getName();
}
