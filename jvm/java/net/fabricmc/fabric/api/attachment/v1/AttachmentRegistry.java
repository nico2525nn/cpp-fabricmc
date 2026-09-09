package net.fabricmc.fabric.api.attachment.v1;

import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;
import java.util.function.Supplier;
import com.mojang.serialization.Codec;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.util.Identifier;

/** Registry and builder for server-side data attachments. */
public final class AttachmentRegistry {
    private static final Map<Identifier, AttachmentType<?>> TYPES = new ConcurrentHashMap<>();

    private AttachmentRegistry() { }

    public static <A> AttachmentType<A> create(Identifier id,
                                                Consumer<Builder<A>> configurator) {
        BuilderImpl<A> builder = new BuilderImpl<>();
        Objects.requireNonNull(configurator, "configurator").accept(builder);
        return builder.buildAndRegister(id);
    }

    public static <A> AttachmentType<A> create(Identifier id) {
        return AttachmentRegistry.<A>builderFor().buildAndRegister(id);
    }

    public static <A> AttachmentType<A> createDefaulted(Identifier id, Supplier<A> initializer) {
        return AttachmentRegistry.<A>builderFor().initializer(initializer).buildAndRegister(id);
    }

    public static <A> AttachmentType<A> createPersistent(Identifier id, Codec<A> codec) {
        return AttachmentRegistry.<A>builderFor().persistent(codec).buildAndRegister(id);
    }

    public static <A> Builder<A> builder() {
        return new BuilderImpl<>();
    }

    /** Internal lookup used by persistence and synchronization adapters. */
    public static AttachmentType<?> get(Identifier id) {
        return TYPES.get(id);
    }

    public static void clear() {
        TYPES.clear();
    }

    private static <A> BuilderImpl<A> builderFor() {
        return new BuilderImpl<>();
    }

    public interface Builder<A> {
        Builder<A> persistent(Codec<A> codec);
        Builder<A> copyOnDeath();
        Builder<A> initializer(Supplier<A> initializer);
        Builder<A> syncWith(PacketCodec<? super RegistryByteBuf, A> codec,
                            AttachmentSyncPredicate predicate);
        AttachmentType<A> buildAndRegister(Identifier id);
    }

    private static final class BuilderImpl<A> implements Builder<A> {
        private Codec<A> persistenceCodec;
        private Supplier<A> initializer;
        private PacketCodec<? super RegistryByteBuf, A> packetCodec;
        private AttachmentSyncPredicate syncPredicate;
        private boolean copyOnDeath;

        @Override public Builder<A> persistent(Codec<A> codec) {
            persistenceCodec = Objects.requireNonNull(codec, "codec");
            return this;
        }

        @Override public Builder<A> copyOnDeath() {
            copyOnDeath = true;
            return this;
        }

        @Override public Builder<A> initializer(Supplier<A> value) {
            initializer = Objects.requireNonNull(value, "initializer");
            return this;
        }

        @Override public Builder<A> syncWith(PacketCodec<? super RegistryByteBuf, A> codec,
                                              AttachmentSyncPredicate predicate) {
            packetCodec = Objects.requireNonNull(codec, "codec");
            syncPredicate = Objects.requireNonNull(predicate, "predicate");
            return this;
        }

        @Override public AttachmentType<A> buildAndRegister(Identifier id) {
            AttachmentType<A> type = new TypeImpl<>(
                Objects.requireNonNull(id, "id"), initializer, persistenceCodec,
                packetCodec, syncPredicate, copyOnDeath);
            TYPES.put(id, type);
            return type;
        }
    }

    /** Extra implementation accessors are useful to the native sync bridge. */
    public record TypeImpl<A>(Identifier identifier, Supplier<A> initializer,
                             Codec<A> persistenceCodec,
                             PacketCodec<? super RegistryByteBuf, A> packetCodec,
                             AttachmentSyncPredicate syncPredicate,
                             boolean copyOnDeath) implements AttachmentType<A> {
        @Override public boolean isSynced() { return packetCodec != null; }
    }
}
