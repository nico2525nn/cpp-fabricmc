package cppfm.transformer_fixture;

/** Target used to prove that two injector operations share one ref object. */
public final class ShareTarget {
    public int compute(int input) {
        int local = input + 1;
        return helper(local);
    }

    private static int helper(int value) {
        return value * 2;
    }
}
