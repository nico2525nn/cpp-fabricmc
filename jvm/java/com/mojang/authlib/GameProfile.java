package com.mojang.authlib;

import java.util.UUID;

/** Small GameProfile value object required by ServerPlayerEntity constructors. */
public class GameProfile {
    private final UUID id;
    private final String name;
    public GameProfile(UUID id, String name) {
        this.id = id == null ? new UUID(0L, 0L) : id;
        this.name = name == null ? "" : name;
    }
    public UUID getId() { return id; }
    public String getName() { return name; }
}
