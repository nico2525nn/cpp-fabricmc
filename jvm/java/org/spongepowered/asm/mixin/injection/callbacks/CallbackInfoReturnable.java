package org.spongepowered.asm.mixin.injection.callback;

public class CallbackInfoReturnable<T> extends CallbackInfo {
    private T returnValue;
    public CallbackInfoReturnable(String id, boolean cancellable) { super(id, cancellable); }
    public CallbackInfoReturnable(String id, boolean cancellable, T returnValue) {
        this(id, cancellable); this.returnValue = returnValue;
    }
    public T getReturnValue() { return returnValue; }
    public void setReturnValue(T value) { returnValue = value; cancel(); }
    public byte getReturnValueB() { return ((Number)returnValue).byteValue(); }
    public char getReturnValueC() { return (Character)returnValue; }
    public double getReturnValueD() { return ((Number)returnValue).doubleValue(); }
    public float getReturnValueF() { return ((Number)returnValue).floatValue(); }
    public int getReturnValueI() { return ((Number)returnValue).intValue(); }
    public long getReturnValueJ() { return ((Number)returnValue).longValue(); }
    public short getReturnValueS() { return ((Number)returnValue).shortValue(); }
    public boolean getReturnValueZ() { return (Boolean)returnValue; }
}
