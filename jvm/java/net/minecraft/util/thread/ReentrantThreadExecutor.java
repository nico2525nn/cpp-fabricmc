package net.minecraft.util.thread;

import java.util.ArrayDeque;
import java.util.Queue;

/**
 * Small server-thread executor ABI used by Carpet and similar server mods.
 * Tasks submitted from the owning thread are run synchronously; submissions
 * from another thread are queued until {@link #runTasks()} is called.
 */
public abstract class ReentrantThreadExecutor<R extends Runnable>
        implements java.util.concurrent.Executor {
    private final Queue<R> tasks = new ArrayDeque<>();
    private volatile Thread owner;

    protected ReentrantThreadExecutor(String name) {
        owner = Thread.currentThread();
    }

    protected ReentrantThreadExecutor() {
        this("cppfm");
    }

    protected abstract R createTask(Runnable runnable);

    public boolean isOnThread() {
        return Thread.currentThread() == owner;
    }

    protected boolean shouldRun(R task) {
        return true;
    }

    @Override
    public void execute(Runnable runnable) {
        if (runnable == null) return;
        R task = createTask(runnable);
        if (isOnThread()) {
            if (shouldRun(task)) task.run();
        } else {
            synchronized (tasks) { tasks.add(task); }
        }
    }

    public boolean runTask() {
        R task;
        synchronized (tasks) { task = tasks.poll(); }
        if (task == null) return false;
        if (shouldRun(task)) task.run();
        return true;
    }

    public int runTasks() {
        int count = 0;
        while (runTask()) ++count;
        return count;
    }

    public boolean shouldExecuteAsync() {
        return !isOnThread();
    }

    protected void setOwnerThread(Thread thread) {
        if (thread != null) owner = thread;
    }
}
