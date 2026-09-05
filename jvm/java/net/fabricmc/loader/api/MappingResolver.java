package net.fabricmc.loader.api;

public interface MappingResolver {
    MappingResolver IDENTITY = new MappingResolver() {
        private void checkNamespace(String namespace) {
            if (!"named".equals(namespace)) {
                throw new IllegalArgumentException("cppfm shadow supports only the named namespace: " + namespace);
            }
        }
        public String mapClassName(String namespace, String className) { checkNamespace(namespace); return className; }
        public String mapFieldName(String namespace, String owner, String name, String descriptor) { checkNamespace(namespace); return name; }
        public String mapMethodName(String namespace, String owner, String name, String descriptor) { checkNamespace(namespace); return name; }
        public String unmapClassName(String namespace, String className) { checkNamespace(namespace); return className; }
        public String getCurrentRuntimeNamespace() { return "named"; }
    };
    String mapClassName(String namespace, String className);
    String mapFieldName(String namespace, String owner, String name, String descriptor);
    String mapMethodName(String namespace, String owner, String name, String descriptor);
    String unmapClassName(String namespace, String className);
    String getCurrentRuntimeNamespace();
}
