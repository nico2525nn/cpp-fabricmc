package cppfm.transformer_fixture;

/** Target methods for the MixinExtras structural-operation contract. */
public final class ExtrasTarget {
    private int calls;

    public int returnValue(int value) {
        return value + 1;
    }

    public int expression(int value) {
        return helper(value);
    }

    public int conditional(int value) {
        helper(value);
        return calls;
    }

    public int wrapped(int value) {
        return helper(value);
    }

    public int wrappedMethod(int value) {
        return value + 4;
    }

    private int helper(int value) {
        calls += value;
        return value * 2;
    }
}
