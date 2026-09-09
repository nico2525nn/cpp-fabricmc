package net.minecraft.resource;

import java.util.Optional;
import net.minecraft.registry.VersionedIdentifier;
import net.minecraft.text.Text;

/** Immutable display and provenance information for a resource pack. */
public record ResourcePackInfo(String id, Text title, ResourcePackSource source,
                               Optional<VersionedIdentifier> knownPackInfo) {
    public ResourcePackInfo {
        id = id == null ? "" : id;
        title = title == null ? Text.empty() : title;
        source = source == null ? ResourcePackSource.NONE : source;
        knownPackInfo = knownPackInfo == null ? Optional.empty() : knownPackInfo;
    }

    public Text getInformationText(boolean enabled, Text description) {
        Text base = description == null ? title : description;
        return source.decorate(base == null ? Text.empty() : base);
    }
}
