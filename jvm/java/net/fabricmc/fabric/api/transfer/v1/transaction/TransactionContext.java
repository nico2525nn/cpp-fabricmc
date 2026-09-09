package net.fabricmc.fabric.api.transfer.v1.transaction;

/** Participant-facing portion of a Fabric transfer transaction. */
public interface TransactionContext {
    Transaction openNested();
    int nestingDepth();
    Transaction getOpenTransaction(int nestingDepth);
    void addCloseCallback(CloseCallback closeCallback);
    void addOuterCloseCallback(OuterCloseCallback outerCloseCallback);

    @FunctionalInterface
    interface CloseCallback {
        void onClose(TransactionContext transaction, Result result);
    }

    @FunctionalInterface
    interface OuterCloseCallback {
        void afterOuterClose(Result result);
    }

    enum Result {
        ABORTED,
        COMMITTED;

        public boolean wasAborted() { return this == ABORTED; }
        public boolean wasCommitted() { return this == COMMITTED; }
    }
}
