package net.minecraft.util;

import java.util.Objects;

/** Result plus value used by item and interaction APIs. */
public final class TypedActionResult<T> {
    private final ActionResult result;
    private final T value;

    public TypedActionResult(ActionResult result, T value) {
        this.result = Objects.requireNonNull(result, "result");
        this.value = value;
    }
    public ActionResult getResult() { return result; }
    public T getValue() { return value; }
    public boolean isAccepted() { return result.isAccepted(); }
    public static <T> TypedActionResult<T> success(T value) { return new TypedActionResult<>(ActionResult.SUCCESS, value); }
    public static <T> TypedActionResult<T> success(T value, boolean client) {
        return new TypedActionResult<>(client ? ActionResult.SUCCESS_CLIENT : ActionResult.SUCCESS_SERVER, value);
    }
    public static <T> TypedActionResult<T> pass(T value) { return new TypedActionResult<>(ActionResult.PASS, value); }
    public static <T> TypedActionResult<T> fail(T value) { return new TypedActionResult<>(ActionResult.FAIL, value); }
}
