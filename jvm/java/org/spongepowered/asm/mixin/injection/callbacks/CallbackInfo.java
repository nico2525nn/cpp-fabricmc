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
        if (!cancellable) throw new CancellationException("The call is not cancellable");
        cancelled = true;
    }
    public static class CancellationException extends RuntimeException {
        private static final long serialVersionUID = 1L;
        public CancellationException() { super(); }
        public CancellationException(String message) { super(message); }
    }
}
