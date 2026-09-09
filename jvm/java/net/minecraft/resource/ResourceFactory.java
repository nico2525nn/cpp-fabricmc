package net.minecraft.resource;

import java.util.Optional;
import net.minecraft.util.Identifier;

/** Single-resource lookup contract shared by vanilla resource managers. */
public interface ResourceFactory {
    Optional<Resource> getResource(Identifier id);
}
