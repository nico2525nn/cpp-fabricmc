package cppfm.corpus.fixture08;

/**
 * Deliberately narrow class-file surface used by the Access Widener contract.
 * The source itself must remain inaccessible/final; only transformed bytes
 * make the public/protected API visible to the focused test.
 */
final class AccessTarget {
    private final int mutableValue = Integer.valueOf(7);
    private static final Integer accessibleFinal = Integer.valueOf(11);

    private AccessTarget() {
    }

    public int readMutable() {
        return mutableValue;
    }

    public int callOverridable(int value) {
        return overridable(value);
    }

    private int secret(int value) {
        return value + 4;
    }

    private int overridable(int value) {
        return value + 8;
    }

    private int descriptorTarget(String value) {
        return value == null ? -1 : value.length();
    }
}
