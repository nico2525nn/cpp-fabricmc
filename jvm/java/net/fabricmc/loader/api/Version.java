package net.fabricmc.loader.api;

/** Version value exposed by Fabric Loader metadata. */
public interface Version extends Comparable<Version> {
    default String getFriendlyString() { return toString(); }

    static Version parse(String value) throws VersionParsingException {
        if (value == null || value.isBlank()) throw new VersionParsingException("empty version");
        return FabricLoader.BasicVersion.parse(value);
    }

    @Override
    default int compareTo(Version other) {
        return toString().compareTo(other == null ? "" : other.toString());
    }
}
