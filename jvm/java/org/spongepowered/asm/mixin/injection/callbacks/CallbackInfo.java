package org.spongepowered.asm.mixin.injection.callback;

public class CallbackInfo {
    private final String id;
    private final boolean cancellable;
    private boolean cancelled;

    public CallbackInfo(String id, boolean cancellable) {
        this.id = id; this.cancellable = cancellable;
    }
    public String getId() { return id; }
    public boolean isCancellable() { return cancellable; }
    public boolean isCancelled() { return cancelled; }
    public void cancel() {
        if (!cancellable) throw new CancellationException("The callback is not cancellable");
        cancelled = true;
    }
    public static class CancellationException extends RuntimeException {
        public CancellationException(String message) { super(message); }
    }
}
