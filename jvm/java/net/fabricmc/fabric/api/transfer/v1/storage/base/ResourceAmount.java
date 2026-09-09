package net.fabricmc.fabric.api.transfer.v1.storage.base;

/** Immutable resource/amount pair used for snapshots and extraction results. */
public record ResourceAmount<T>(T resource, long amount) { }
