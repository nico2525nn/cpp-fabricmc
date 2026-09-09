package net.fabricmc.fabric.api.attachment.v1;

import java.util.function.Supplier;
import com.mojang.serialization.Codec;
import net.minecraft.util.Identifier;

/** A typed value that can be attached to a supported game object. */
public interface AttachmentType<A> {
    Identifier identifier();
    Codec<A> persistenceCodec();

    default boolean isPersistent() {
        return persistenceCodec() != null;
    }

    Supplier<A> initializer();
    boolean isSynced();
    boolean copyOnDeath();
}
