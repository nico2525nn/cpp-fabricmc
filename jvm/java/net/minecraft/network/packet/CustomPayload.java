package net.minecraft.network.packet;

import net.minecraft.util.Identifier;

public interface CustomPayload {
    Id<? extends CustomPayload> getId();
    record Id<T extends CustomPayload>(Identifier id) { public Id { if (id == null) throw new NullPointerException("id"); } }
}
