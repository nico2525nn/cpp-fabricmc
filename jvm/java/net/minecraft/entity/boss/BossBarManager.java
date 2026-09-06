package net.minecraft.entity.boss;

import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.Map;
import net.minecraft.util.Identifier;

/** Registry of command boss bars. */
public class BossBarManager {
    private final Map<Identifier, CommandBossBar> bars = new LinkedHashMap<>();
    public CommandBossBar add(Identifier id, net.minecraft.text.Text name) {
        return bars.computeIfAbsent(id, key -> new CommandBossBar(key, name));
    }
    public CommandBossBar get(Identifier id) { return bars.get(id); }
    public Collection<CommandBossBar> getBars() { return bars.values(); }
    public void remove(Identifier id) { bars.remove(id); }
}
