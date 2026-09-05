package net.fabricmc.loader.api;

public interface MappingResolver {
    MappingResolver IDENTITY = new MappingResolver() {
        public String mapClassName(String namespace, String className) { return className; }
        public String mapFieldName(String namespace, String owner, String name, String descriptor) { return name; }
        public String mapMethodName(String namespace, String owner, String name, String descriptor) { return name; }
        public String unmapClassName(String namespace, String className) { return className; }
        public String getCurrentRuntimeNamespace() { return "named"; }
    };
    String mapClassName(String namespace, String className);
    String mapFieldName(String namespace, String owner, String name, String descriptor);
    String mapMethodName(String namespace, String owner, String name, String descriptor);
    String unmapClassName(String namespace, String className);
    String getCurrentRuntimeNamespace();
}
