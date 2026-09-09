package net.fabricmc.fabric.api.transfer.v1.transaction;

import java.util.ArrayList;
import java.util.List;

/**
 * Thread-confined nested transaction stack used by Fabric Transfer API
 * participants.  A close without an explicit commit aborts the transaction.
 */
public interface Transaction extends AutoCloseable, TransactionContext {
    static Transaction openOuter() { return TransactionState.openOuter(); }
    static boolean isOpen() { return getLifecycle() != Lifecycle.NONE; }
    static Lifecycle getLifecycle() { return TransactionState.lifecycle(); }
    static Transaction openNested(TransactionContext parent) {
        return parent == null ? openOuter() : TransactionState.openNested(parent);
    }
    @Deprecated
    static TransactionContext getCurrentUnsafe() { return TransactionState.current(); }

    void abort();
    void commit();
    @Override void close();

    enum Lifecycle { NONE, OPEN, CLOSING, OUTER_CLOSING }

    /** Package-independent implementation kept behind the stable interface. */
    final class TransactionState implements Transaction {
        private static final ThreadLocal<List<TransactionState>> STACK =
            ThreadLocal.withInitial(ArrayList::new);
        private final TransactionState parent;
        private final int depth;
        private final List<CloseCallback> closeCallbacks = new ArrayList<>();
        private final List<OuterCloseCallback> outerCallbacks = new ArrayList<>();
        private Lifecycle lifecycle = Lifecycle.OPEN;
        private boolean closed;

        private TransactionState(TransactionState parent) {
            this.parent = parent;
            this.depth = STACK.get().size();
        }

        private static Transaction openOuter() {
            List<TransactionState> stack = STACK.get();
            if (!stack.isEmpty()) throw new IllegalStateException("An outer transaction is already open");
            TransactionState transaction = new TransactionState(null);
            stack.add(transaction);
            return transaction;
        }

        private static Transaction openNested(TransactionContext parentContext) {
            List<TransactionState> stack = STACK.get();
            if (stack.isEmpty() || stack.get(stack.size() - 1) != parentContext)
                throw new IllegalStateException("The parent transaction is not current");
            TransactionState parent = (TransactionState) parentContext;
            TransactionState transaction = new TransactionState(parent);
            stack.add(transaction);
            return transaction;
        }

        private static TransactionContext current() {
            List<TransactionState> stack = STACK.get();
            return stack.isEmpty() ? null : stack.get(stack.size() - 1);
        }

        private static Lifecycle lifecycle() {
            List<TransactionState> stack = STACK.get();
            return stack.isEmpty() ? Lifecycle.NONE : stack.get(stack.size() - 1).lifecycle;
        }

        private void assertCurrent() {
            List<TransactionState> stack = STACK.get();
            if (closed || stack.isEmpty() || stack.get(stack.size() - 1) != this)
                throw new IllegalStateException("Transaction is not current or was closed");
        }

        @Override public Transaction openNested() {
            assertCurrent();
            return openNested(this);
        }

        @Override public int nestingDepth() { assertThread(); return depth; }

        @Override public Transaction getOpenTransaction(int requestedDepth) {
            assertThread();
            List<TransactionState> stack = STACK.get();
            if (requestedDepth < 0 || requestedDepth >= stack.size())
                throw new IndexOutOfBoundsException("No transaction at depth " + requestedDepth);
            return stack.get(requestedDepth);
        }

        @Override public void addCloseCallback(CloseCallback callback) {
            assertThread();
            if (callback == null) throw new NullPointerException("callback");
            closeCallbacks.add(callback);
        }

        @Override public void addOuterCloseCallback(OuterCloseCallback callback) {
            assertThread();
            if (callback == null) throw new NullPointerException("callback");
            outerCallbacks.add(callback);
        }

        private void assertThread() {
            if (closed || STACK.get().stream().noneMatch(value -> value == this))
                throw new IllegalStateException("Transaction is closed or used from another thread");
        }

        @Override public void abort() { finish(Result.ABORTED); }
        @Override public void commit() { finish(Result.COMMITTED); }

        private void finish(Result result) {
            assertCurrent();
            lifecycle = Lifecycle.CLOSING;
            RuntimeException failure = null;
            for (int index = closeCallbacks.size() - 1; index >= 0; index--) {
                try { closeCallbacks.get(index).onClose(this, result); }
                catch (RuntimeException exception) { if (failure == null) failure = exception; }
            }
            closeCallbacks.clear();
            STACK.get().remove(STACK.get().size() - 1);
            closed = true;
            if (parent == null) {
                lifecycle = Lifecycle.OUTER_CLOSING;
                for (int index = outerCallbacks.size() - 1; index >= 0; index--) {
                    try { outerCallbacks.get(index).afterOuterClose(result); }
                    catch (RuntimeException exception) { if (failure == null) failure = exception; }
                }
                outerCallbacks.clear();
            }
            lifecycle = Lifecycle.NONE;
            if (failure != null) throw failure;
        }

        @Override public void close() {
            if (!closed) abort();
        }
    }
}
