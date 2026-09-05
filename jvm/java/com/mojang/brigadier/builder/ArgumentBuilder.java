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
    protected final List<CommandNode<S>> childNodes = new ArrayList<>();
    protected Command<S> command;
    protected Predicate<S> requirement = source -> true;
    public T then(ArgumentBuilder<S, ?> child) { if (child != null) children.add(child); return (T) this; }
    public T then(CommandNode<S> child) { if (child != null) childNodes.add(child); return (T) this; }
    public T executes(Command<S> command) { this.command = command; return (T) this; }
    public List<ArgumentBuilder<S, ?>> getChildren() { return List.copyOf(children); }
    public Collection<CommandNode<S>> getArguments() {
        List<CommandNode<S>> result = new ArrayList<>();
        for (ArgumentBuilder<S, ?> child : children) result.add(child.build());
        result.addAll(childNodes);
        return List.copyOf(result);
    }
    public Command<S> getCommand() { return command; }
    public T requires(Predicate<S> requirement) { this.requirement = requirement == null ? source -> true : requirement; return (T) this; }
    public Predicate<S> getRequirement() { return requirement; }
    protected CommandNode<S> redirect;
    protected RedirectModifier<S> redirectModifier;
    protected boolean forks;
    public T redirect(CommandNode<S> target) { redirect = target; redirectModifier = null; forks = false; return (T) this; }
    public T redirect(CommandNode<S> target, SingleRedirectModifier<S> modifier) { redirect = target; redirectModifier = modifier == null ? null : context -> java.util.List.of(modifier.apply(context)); forks = false; return (T) this; }
    public T fork(CommandNode<S> target, RedirectModifier<S> modifier) { redirect = target; redirectModifier = modifier; forks = true; return (T) this; }
    public T forward(CommandNode<S> target, RedirectModifier<S> modifier, boolean forks) { redirect = target; redirectModifier = modifier; this.forks = forks; return (T) this; }
    public abstract CommandNode<S> build();
    public abstract String getName();
}
