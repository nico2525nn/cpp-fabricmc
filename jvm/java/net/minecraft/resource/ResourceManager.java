package net.minecraft.resource;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Predicate;
import java.util.stream.Stream;
import net.minecraft.util.Identifier;

/** Resource lookup contract used by server-side reload listeners. */
public interface ResourceManager extends ResourceFactory {
    @Override
    default Optional<Resource> getResource(Identifier id) { return Optional.empty(); }
    default List<Resource> getAllResources(Identifier id) { return List.of(); }
    default Map<Identifier, Resource> findResources(String startingPath,
                                                     Predicate<String> allowedPathPredicate) {
        return Map.of();
    }
    default Map<Identifier, List<Resource>> findAllResources(String startingPath,
                                                              Predicate<String> allowedPathPredicate) {
        return Map.of();
    }
    default Set<String> getAllNamespaces() { return Set.of(); }
    default Stream<ResourcePack> streamResourcePacks() { return Stream.empty(); }

    /** Empty manager singleton exposed by vanilla. */
    final class Empty implements ResourceManager {
        public static final Empty INSTANCE = new Empty();
        private Empty() { }
    }
}
