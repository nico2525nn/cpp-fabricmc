package net.fabricmc.fabric.api.transfer.v1.transaction.base;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** Base participant that keeps one snapshot per open transaction depth. */
public abstract class SnapshotParticipant<T>
        implements TransactionContext.CloseCallback, TransactionContext.OuterCloseCallback {
    private final List<T> snapshots = new ArrayList<>();

    protected abstract T createSnapshot();
    protected abstract void readSnapshot(T snapshot);
    protected void releaseSnapshot(T snapshot) { }
    protected void onFinalCommit() { }

    public void updateSnapshots(TransactionContext transaction) {
        Objects.requireNonNull(transaction, "transaction");
        while (snapshots.size() <= transaction.nestingDepth()) snapshots.add(null);
        int depth = transaction.nestingDepth();
        if (snapshots.get(depth) == null) {
            T snapshot = Objects.requireNonNull(createSnapshot(), "Snapshot may not be null");
            snapshots.set(depth, snapshot);
            transaction.addCloseCallback(this);
        }
    }

    @Override public void onClose(TransactionContext transaction, TransactionContext.Result result) {
        int depth = transaction.nestingDepth();
        T snapshot = snapshots.set(depth, null);
        if (snapshot == null) return;
        if (result.wasAborted()) {
            readSnapshot(snapshot);
            releaseSnapshot(snapshot);
        } else if (depth > 0) {
            if (snapshots.get(depth - 1) == null) {
                snapshots.set(depth - 1, snapshot);
                transaction.getOpenTransaction(depth - 1).addCloseCallback(this);
            } else {
                releaseSnapshot(snapshot);
            }
        } else {
            releaseSnapshot(snapshot);
            transaction.addOuterCloseCallback(this);
        }
    }

    @Override public void afterOuterClose(TransactionContext.Result result) {
        if (result.wasCommitted()) onFinalCommit();
    }
}
