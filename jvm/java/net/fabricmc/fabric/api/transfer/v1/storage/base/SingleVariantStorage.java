package net.fabricmc.fabric.api.transfer.v1.storage.base;

import net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions;
import net.fabricmc.fabric.api.transfer.v1.storage.TransferVariant;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.fabricmc.fabric.api.transfer.v1.transaction.base.SnapshotParticipant;

/** Transaction-safe storage containing at most one transfer variant. */
public abstract class SingleVariantStorage<T extends TransferVariant<?>>
        extends SnapshotParticipant<ResourceAmount<T>> implements SingleSlotStorage<T> {
    public T variant = getBlankVariant();
    public long amount;

    protected abstract T getBlankVariant();
    protected abstract long getCapacity(T variant);
    protected boolean canInsert(T variant) { return true; }
    protected boolean canExtract(T variant) { return true; }

    @Override public long insert(T insertedVariant, long maxAmount, TransactionContext transaction) {
        StoragePreconditions.notBlankNotNegative(insertedVariant, maxAmount);
        if ((variant.isBlank() || insertedVariant.equals(variant)) && canInsert(insertedVariant)) {
            long accepted = Math.min(maxAmount, Math.max(0, getCapacity(insertedVariant) - amount));
            if (accepted > 0) {
                updateSnapshots(transaction);
                if (variant.isBlank()) variant = insertedVariant;
                amount += accepted;
                return accepted;
            }
        }
        return 0;
    }

    @Override public long extract(T extractedVariant, long maxAmount, TransactionContext transaction) {
        StoragePreconditions.notBlankNotNegative(extractedVariant, maxAmount);
        if (extractedVariant.equals(variant) && canExtract(extractedVariant)) {
            long removed = Math.min(maxAmount, amount);
            if (removed > 0) {
                updateSnapshots(transaction);
                amount -= removed;
                if (amount == 0) variant = getBlankVariant();
                return removed;
            }
        }
        return 0;
    }

    @Override public boolean isResourceBlank() { return variant.isBlank(); }
    @Override public T getResource() { return variant; }
    @Override public long getAmount() { return amount; }
    @Override public long getCapacity() { return getCapacity(variant); }
    @Override protected ResourceAmount<T> createSnapshot() { return new ResourceAmount<>(variant, amount); }
    @Override protected void readSnapshot(ResourceAmount<T> snapshot) { variant = snapshot.resource(); amount = snapshot.amount(); }
    @Override public String toString() { return "SingleVariantStorage[" + amount + " " + variant + "]"; }

    public static <T extends TransferVariant<?>> void readNbt(
            SingleVariantStorage<T> storage, com.mojang.serialization.Codec<T> codec,
            java.util.function.Supplier<T> fallback, net.minecraft.nbt.NbtCompound nbt,
            net.minecraft.registry.RegistryWrapper.WrapperLookup lookup) {
        // The native persistence path owns the authoritative NBT schema.  Keep
        // the suggested API safe: invalid or absent data restores the blank
        // variant and never leaves a negative amount.
        storage.variant = fallback.get();
        storage.amount = nbt == null ? 0 : Math.max(0, nbt.getLong("amount", 0));
    }

    public static <T extends TransferVariant<?>> void writeNbt(
            SingleVariantStorage<T> storage, com.mojang.serialization.Codec<T> codec,
            net.minecraft.nbt.NbtCompound nbt, net.minecraft.registry.RegistryWrapper.WrapperLookup lookup) {
        if (nbt != null) nbt.putLong("amount", Math.max(0, storage.amount));
    }
}
