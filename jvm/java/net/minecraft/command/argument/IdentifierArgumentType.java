package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.List;
import net.minecraft.util.Identifier;

/** Brigadier argument for a namespaced Minecraft identifier. */
public final class IdentifierArgumentType implements ArgumentType<Identifier> {
    private static final List<String> EXAMPLES = List.of("minecraft:stone", "example:widget", "stone");
    private IdentifierArgumentType() { }

    public static IdentifierArgumentType identifier() { return new IdentifierArgumentType(); }

    public static Identifier getIdentifier(CommandContext<?> context, String name) {
        Identifier value = context == null ? null : context.getArgument(name, Identifier.class);
        if (value == null) throw new IllegalArgumentException("argument is not an identifier: " + name);
        return value;
    }

    @Override public Identifier parse(StringReader reader) throws CommandSyntaxException {
        String value = reader.readString();
        Identifier identifier = Identifier.tryParse(value);
        if (identifier == null)
            throw new CommandSyntaxException("Invalid identifier: " + value, reader, reader.getCursor());
        return identifier;
    }

    @Override public java.util.Collection<String> getExamples() { return EXAMPLES; }
}
