package net.minecraft.command;

import com.mojang.brigadier.StringReader;
import net.fabricmc.fabric.api.command.v2.FabricEntitySelectorReader;

/** Small selector reader retaining the custom-option state used by Fabric. */
public class EntitySelectorReader implements FabricEntitySelectorReader {
    private final StringReader reader;
    private final boolean allowAtSelectors;

    public EntitySelectorReader(StringReader reader, boolean allowAtSelectors) {
        this.reader = reader == null ? new StringReader("") : reader;
        this.allowAtSelectors = allowAtSelectors;
    }

    public EntitySelectorReader(StringReader reader) { this(reader, true); }
    public StringReader getReader() { return reader; }
    public boolean shouldAllowAtSelectors(Object source) { return allowAtSelectors; }
    public void readArguments() { }
}
