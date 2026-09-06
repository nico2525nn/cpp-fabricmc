package net.minecraft.network;

import net.minecraft.network.packet.Packet;

/** Transport placeholder for the server-side network handler ABI. */
public class ClientConnection {
    private boolean open = true;
    public void send(Packet<?> packet) { }
    public void disconnect() { open = false; }
    public boolean isOpen() { return open; }
}
