package cppfm.transformer_fixture;

/** Package-private target used to prove loader-level Access Widener ordering. */
final class LoaderAccessTarget {
    private final int hidden;

    private LoaderAccessTarget() {
        hidden = 4;
    }

    private int secret(int value) {
        return hidden + value;
    }
}
