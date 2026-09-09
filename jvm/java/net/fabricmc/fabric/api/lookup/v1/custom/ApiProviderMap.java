package net.fabricmc.fabric.api.lookup.v1.custom;

import java.util.concurrent.ConcurrentHashMap;

public interface ApiProviderMap<K, V> {
    static <K, V> ApiProviderMap<K, V> create() { return new Impl<>(); }
    V get(K key);
    V putIfAbsent(K key, V value);

    final class Impl<K, V> implements ApiProviderMap<K, V> {
        private final ConcurrentHashMap<K, V> values = new ConcurrentHashMap<>();
        @Override public V get(K key) { return values.get(key); }
        @Override public V putIfAbsent(K key, V value) { return values.putIfAbsent(key, value); }
    }
}
