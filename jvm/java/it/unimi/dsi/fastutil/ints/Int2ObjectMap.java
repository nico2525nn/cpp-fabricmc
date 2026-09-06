package it.unimi.dsi.fastutil.ints;

/** Small int-keyed map ABI required by the game-event mixin surface. */
public interface Int2ObjectMap<V> {
    V get(int key);
    V put(int key, V value);
    V remove(int key);
    boolean isEmpty();
}
