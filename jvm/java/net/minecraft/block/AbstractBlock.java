package net.minecraft.block;

/** Minimal immutable settings carrier used by common server-side mods. */
public abstract class AbstractBlock {
    private AbstractBlock() {}

    public static class Settings {
        protected Settings() {}
        public static Settings create() { return new Settings(); }
        public Settings strength(float hardness) { return this; }
        public Settings strength(float hardness, float resistance) { return this; }
        public Settings requiresTool() { return this; }
        public Settings nonOpaque() { return this; }
        public Settings luminance(int value) { return this; }
        public Settings dropsNothing() { return this; }
    }
}
