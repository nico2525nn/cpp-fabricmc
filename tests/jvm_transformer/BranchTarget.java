package cppfm.transformer_fixture;

/** Target deliberately contains branches and a try/catch Code attribute. */
public class BranchTarget {
    public int compute(int value) {
        int result;
        try {
            if (value < 0) throw new IllegalArgumentException("negative");
            result = value + 1;
        } catch (RuntimeException failure) {
            result = 7;
        }
        return result;
    }

    public int call(int value) {
        return helper(value);
    }

    public int helper(int value) {
        return value + 2;
    }

    public int constant() {
        return 3;
    }
}
