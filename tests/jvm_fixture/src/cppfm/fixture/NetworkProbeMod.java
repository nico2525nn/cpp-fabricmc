package cppfm.fixture;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Receives a real Mineflayer custom-payload packet through Fabric's typed API. */
public final class NetworkProbeMod implements DedicatedServerModInitializer {
    private static final CustomPayload.Id<ProbePayload> PAYLOAD_ID =
        new CustomPayload.Id<>(Identifier.of(
            "cppfm_fixture", "mineflayer_probe"));

    private static final PacketCodec<RegistryByteBuf, ProbePayload> CODEC =
        PacketCodec.ofStatic(
            (buffer, payload) -> buffer.writeString(payload.value()),
            buffer -> new ProbePayload(buffer.readString()));

    private record ProbePayload(String value) implements CustomPayload {
        @Override
        public CustomPayload.Id<? extends CustomPayload> getId() {
            return PAYLOAD_ID;
        }
    }

    @Override
    public void onInitializeServer() {
        PayloadTypeRegistry.playC2S().register(PAYLOAD_ID, CODEC);
        boolean registered = ServerPlayNetworking.registerGlobalReceiver(
            PAYLOAD_ID,
            (payload, context) -> NativeBridge.nativeLog("INFO",
                "fixture MINEFLAYER_PLAY_PAYLOAD player=" +
                    context.player().getName().getString() +
                    " value=" + payload.value()));
        NativeBridge.nativeLog("INFO",
            "fixture MINEFLAYER_PLAY_PAYLOAD_REGISTERED " + registered);
    }
}
