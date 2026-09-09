package net.minecraft.network.packet.c2s.login;

import com.mojang.authlib.GameProfile;
import java.util.UUID;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.Packet;

/** Minimal named ABI for the 1.21.4 login hello packet. */
public final class LoginHelloC2SPacket implements Packet<Object> {
    private final String name;
    private final UUID profileId;

    public LoginHelloC2SPacket() {
        this("", new UUID(0L, 0L));
    }

    public LoginHelloC2SPacket(String name, UUID profileId) {
        this.name = name == null ? "" : name;
        this.profileId = profileId == null ? new UUID(0L, 0L) : profileId;
    }

    public LoginHelloC2SPacket(PacketByteBuf buf) {
        this();
    }

    public String name() { return name; }
    public UUID profileId() { return profileId; }
    public GameProfile profile() { return new GameProfile(profileId, name); }

    @Override
    public void apply(Object listener) { }
}
