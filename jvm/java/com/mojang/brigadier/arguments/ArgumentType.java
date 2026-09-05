package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public interface ArgumentType<T> {
    T parse(StringReader reader) throws CommandSyntaxException;
}
