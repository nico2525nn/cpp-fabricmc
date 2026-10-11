package cppfm.loader;

import java.nio.file.Path;

/** Command-line adapter used by the isolated Knot verifier. */
public final class ModJarStagerMain {
    private ModJarStagerMain() { }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException(
                "usage: ModJarStagerMain <yarn-mappings.jar> <mods-source> <mods-destination>");
        }
        IntermediaryNamedMappings mappings = IntermediaryNamedMappings.load(Path.of(args[0]));
        ModJarStager.Result result = ModJarStager.stage(
            Path.of(args[1]), Path.of(args[2]), mappings);
        System.out.println("CPPFM_MODS_STAGED mods=" + result.modCount()
            + " classes=" + result.classCount() + " resources=" + result.resourceCount());
    }
}
