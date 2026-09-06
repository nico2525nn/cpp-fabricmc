package net.minecraft.entity.boss;

import net.minecraft.text.Text;
import net.minecraft.util.Identifier;

/** Command-owned boss bar. */
public class CommandBossBar extends BossBar {
    private final Identifier id;
    public CommandBossBar(Identifier id, Text displayName) { super(displayName); this.id = id; }
    public Identifier getId() { return id; }
}
