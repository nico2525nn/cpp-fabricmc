package net.minecraft.world.chunk;

import java.util.List;
import net.minecraft.util.collection.IdList;

/** Structural 1.21.4 paletted-container ABI for chunk and Lithium mixins. */
public class PalettedContainer<T> {
    protected PaletteProvider paletteProvider;
    protected IdList<T> idList;
    protected Data data;

    public PalettedContainer() { this(new IdList<>(), PaletteProvider.BLOCK_STATE, new DataProvider<>(null, 0), null, List.of()); }
    public PalettedContainer(IdList<T> idList, PaletteProvider paletteProvider,
                             DataProvider<T> dataProvider, Object storage, List<T> entries) {
        this.idList = idList;
        this.paletteProvider = paletteProvider;
        this.data = new Data();
    }
    public T get(int index) { return null; }
    public void set(int index, T value) { }
    public void lock() { }
    public void unlock() { }
    public PalettedContainer<T> copy() { return this; }

    public static class Data { }

    public static class DataProvider<T> {
        protected final Palette.Factory<T> factory;
        protected final int bits;
        public DataProvider(Palette.Factory<T> factory, int bits) { this.factory = factory; this.bits = bits; }
        public int bits() { return bits; }
        public Palette.Factory<T> factory() { return factory; }
        public Data createData(IdList<T> idList, Object listener, int size) { return new Data(); }
    }

    public static class PaletteProvider {
        public static final Palette.Factory<Object> SINGULAR = (bits, ids, listener, entries) -> null;
        public static final Palette.Factory<Object> ARRAY = (bits, ids, listener, entries) -> null;
        public static final Palette.Factory<Object> BI_MAP = (bits, ids, listener, entries) -> null;
        public static final Palette.Factory<Object> ID_LIST = (bits, ids, listener, entries) -> null;
        public static PaletteProvider BLOCK_STATE = new PaletteProvider(4);
        public static PaletteProvider BIOME = new PaletteProvider(2);
        protected final int edgeBits;

        public PaletteProvider(int edgeBits) { this.edgeBits = edgeBits; }
        public int getBits(IdList<?> idList, int size) { return Math.max(edgeBits, size); }
        public int getBits(int size) { return Math.max(edgeBits, size); }
        public DataProvider<?> createDataProvider(IdList<?> idList, int bits) {
            return new DataProvider<>(null, bits);
        }
        public int computeIndex(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
    }
}
