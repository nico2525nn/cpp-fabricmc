package net.minecraft.server.network;

import com.mojang.authlib.GameProfile;
import net.minecraft.network.ClientConnection;
import net.minecraft.network.packet.c2s.login.EnterConfigurationC2SPacket;
import net.minecraft.network.packet.c2s.login.LoginHelloC2SPacket;
import net.minecraft.network.packet.c2s.login.LoginKeyC2SPacket;
import net.minecraft.network.packet.c2s.login.LoginQueryResponseC2SPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.text.Text;

/** Minimal login-phase handler shared by Fabric's login networking API. */
public class ServerLoginNetworkHandler extends ServerCommonNetworkHandler {
    public static final int TIMEOUT_TICKS = 600;
    // Fabric/Krypton's 1.21.4 access widener names this field on the login
    // handler itself (not on ServerCommonNetworkHandler).  Keep the owner
    // declaration here even though the common superclass also retains the
    // connection used by the fallback implementation.
    private final MinecraftServer server;
    private final ClientConnection connection;
    private final boolean transferred;
    private GameProfile profile;
    private int loginTicks;

    public ServerLoginNetworkHandler(MinecraftServer server, ClientConnection connection,
                                     boolean transferred) {
        super(server, connection, new ConnectedClientData());
        this.server = server;
        this.connection = connection == null ? new ClientConnection() : connection;
        this.transferred = transferred;
    }

    public GameProfile getProfile() { return profile; }
    public void setProfile(GameProfile value) { profile = value; }
    public boolean isTransferred() { return transferred; }
    public int getLoginTicks() { return loginTicks; }
    public void tick() { loginTicks++; }
    /** Login-key dispatch target required by the Yarn 1.21.4 listener ABI. */
    public void onKey(LoginKeyC2SPacket packet) { }
    /** Login hello dispatch target required by the Yarn 1.21.4 listener ABI. */
    public void onHello(LoginHelloC2SPacket packet) { }
    /** Login query-response dispatch target used by Fabric login networking. */
    public void onQueryResponse(LoginQueryResponseC2SPacket packet) { }
    /** Configuration transition dispatch target from the login listener. */
    public void onEnterConfiguration(EnterConfigurationC2SPacket packet) { }
    /** Vanilla verification call site redirected by Fabric networking. */
    public void tickVerify(GameProfile value) { }
    public String getConnectionInfo() { return "cppfm"; }
    @Override public void disconnect(Text reason) { super.disconnect(reason); }
}
