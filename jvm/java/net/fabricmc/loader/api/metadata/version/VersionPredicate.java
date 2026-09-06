package net.fabricmc.loader.api.metadata.version;

import java.util.Collection;
import java.util.List;
import java.util.function.Predicate;
import net.fabricmc.loader.api.Version;

/** Version requirement predicate. The loader supplies the concrete parser. */
public interface VersionPredicate extends Predicate<Version> {
    default Collection<? extends PredicateTerm> getTerms() { return List.of(); }
    default VersionInterval getInterval() { return VersionInterval.INFINITE; }

    static VersionPredicate parse(String value) throws net.fabricmc.loader.api.VersionParsingException {
        if (value == null || value.isBlank())
            throw new net.fabricmc.loader.api.VersionParsingException("empty version predicate");
        return version -> net.fabricmc.loader.api.FabricLoader.matchesVersion(version, value);
    }

    static Collection<VersionPredicate> parse(Collection<String> values)
            throws net.fabricmc.loader.api.VersionParsingException {
        if (values == null) return List.of();
        java.util.ArrayList<VersionPredicate> result = new java.util.ArrayList<>();
        for (String value : values) result.add(parse(value));
        return List.copyOf(result);
    }

    interface PredicateTerm { }
}
