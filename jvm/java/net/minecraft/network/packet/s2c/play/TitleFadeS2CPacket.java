package net.minecraft.network.packet.s2c.play;

/** Server title timing packet shell used by Carpet's player API references. */
public class TitleFadeS2CPacket implements net.minecraft.network.packet.Packet<Object> {
    public final int fadeIn;
    public final int stay;
    public final int fadeOut;

    public TitleFadeS2CPacket(int fadeIn, int stay, int fadeOut) {
        this.fadeIn = fadeIn; this.stay = stay; this.fadeOut = fadeOut;
    }
    @Override public void apply(Object listener) { }
}
