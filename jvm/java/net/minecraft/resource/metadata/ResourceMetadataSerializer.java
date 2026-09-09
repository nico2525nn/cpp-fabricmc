package net.minecraft.resource.metadata;

import com.mojang.serialization.Codec;

/** Named codec used to decode one section of a resource's metadata. */
public record ResourceMetadataSerializer<T>(String name, Codec<T> codec) { }
