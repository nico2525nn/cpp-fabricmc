package cppfm.transform;

/** Receives an immutable notification after a class transformation attempt. */
@FunctionalInterface
public interface TransformListener {
    void onTransformed(TransformResult result);
}
