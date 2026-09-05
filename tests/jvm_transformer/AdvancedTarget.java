package cppfm.transformer_fixture;

/** Small methods whose bytecode exercises ordinal, slice, locals and jumps. */
public class AdvancedTarget {
    private int helperCalls;
    public int voidState;

    public int sliced(int value) {
        return helper(value) + helper(value + 1) + helper(value + 2);
    }

    public int helper(int value) {
        helperCalls++;
        return value + 1;
    }

    public int assign(int value) {
        int result = helper(value);
        return result * 2;
    }

    public int cancelAtHead(int value) {
        return value + 10;
    }

    public int cancelAtReturn(int value) {
        return value + 2;
    }

    public void cancelVoid(int value) {
        voidState = value;
    }

    public int constants() {
        int first = 3;
        int second = 3;
        return first + second;
    }

    public int modifyVariable(int value) {
        int local = value + 1;
        return local;
    }

    public int capture(int value) {
        int local = value * 2;
        return local + 1;
    }

    public int mixedCapture(int input) {
        String label = "xy";
        long widened = input * 3L;
        return label.length() + (int) widened;
    }

    public int control(int value) {
        int total = 0;
        for (int index = 0; index < value; index++) total += index;
        return total;
    }
}
