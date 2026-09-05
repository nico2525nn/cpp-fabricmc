package net.minecraft.world;

public final class MutableWorldProperties implements WorldProperties {
    private long time, timeOfDay;
    private boolean raining, thundering;
    public long getTime() { return time; }
    public long getTimeOfDay() { return timeOfDay; }
    public boolean isRaining() { return raining; }
    public boolean isThundering() { return thundering; }
    public void setTime(long value) { time = value; }
    public void setTimeOfDay(long value) { timeOfDay = value; }
    public void setRaining(boolean value) { raining = value; }
    public void setThundering(boolean value) { thundering = value; }
}
