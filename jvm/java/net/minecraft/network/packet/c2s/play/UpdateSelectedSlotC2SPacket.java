package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;

/** Minimal 1.21.4 selected-slot packet ABI used by server mixins. */
public class UpdateSelectedSlotC2SPacket implements Packet<Object> {
    private final int selectedSlot;
    public UpdateSelectedSlotC2SPacket() { this(0); }
    public UpdateSelectedSlotC2SPacket(int selectedSlot) {
        this.selectedSlot = Math.max(0, Math.min(8, selectedSlot));
    }
    public int getSelectedSlot() { return selectedSlot; }
    @Override public void apply(Object listener) { }
}
