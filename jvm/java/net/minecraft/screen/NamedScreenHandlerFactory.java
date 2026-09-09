package net.minecraft.screen;

import net.minecraft.text.Text;

/** Screen factory carrying the title shown to a player. */
public interface NamedScreenHandlerFactory extends ScreenHandlerFactory {
    Text getDisplayName();
}
