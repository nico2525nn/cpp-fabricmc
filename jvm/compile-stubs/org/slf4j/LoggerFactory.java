package org.slf4j;

/** Compile-only declaration; runtime resolves SLF4J from the server libraries. */
public final class LoggerFactory {
    private LoggerFactory() { }
    public static Logger getLogger(Class<?> type) { return null; }
}
