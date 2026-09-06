package net.fabricmc.loader.api.metadata;

import java.util.Collection;
import java.util.List;
import net.fabricmc.loader.api.Version;
import net.fabricmc.loader.api.metadata.version.VersionInterval;
import net.fabricmc.loader.api.metadata.version.VersionPredicate;

/** Public dependency metadata contract used by Fabric Loader 0.16.x. */
public interface ModDependency {
    Kind getKind();
    String getModId();
    boolean matches(Version version);
    Collection<VersionPredicate> getVersionRequirements();
    default List<VersionInterval> getVersionIntervals() { return List.of(); }

    enum Kind {
        DEPENDS("depends", true, false),
        RECOMMENDS("recommends", true, true),
        SUGGESTS("suggests", true, true),
        CONFLICTS("conflicts", false, true),
        BREAKS("breaks", false, true);

        private final String key;
        private final boolean positive;
        private final boolean soft;

        Kind(String key, boolean positive, boolean soft) {
            this.key = key;
            this.positive = positive;
            this.soft = soft;
        }

        public String getKey() { return key; }
        public boolean isPositive() { return positive; }
        public boolean isSoft() { return soft; }

        public static Kind parse(String value) {
            if (value == null) throw new IllegalArgumentException("dependency kind is null");
            for (Kind kind : values()) if (kind.key.equalsIgnoreCase(value)) return kind;
            throw new IllegalArgumentException("unknown dependency kind: " + value);
        }
    }
}
