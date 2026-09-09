package net.minecraft.registry;

import com.mojang.serialization.Codec;

/** Registry-loader descriptors used by Fabric dynamic-registry registration. */
public final class RegistryLoader {
    private RegistryLoader() { }

    public static final class Entry<T> {
        private final RegistryKey<? extends Registry<T>> key;
        private final Codec<T> elementCodec;
        private final boolean requiredNonEmpty;

        public Entry(RegistryKey<? extends Registry<T>> key, Codec<T> elementCodec) {
            this(key, elementCodec, false);
        }
        public Entry(RegistryKey<? extends Registry<T>> key, Codec<T> elementCodec,
                     boolean requiredNonEmpty) {
            this.key = key;
            this.elementCodec = elementCodec;
            this.requiredNonEmpty = requiredNonEmpty;
        }
        public RegistryKey<? extends Registry<T>> key() { return key; }
        public Codec<T> elementCodec() { return elementCodec; }
        public boolean requiredNonEmpty() { return requiredNonEmpty; }
    }
}
