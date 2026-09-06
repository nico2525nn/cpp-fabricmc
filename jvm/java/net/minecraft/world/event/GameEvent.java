package net.minecraft.world.event;

/** Minimal game-event value type used by the 1.21.4 event dispatcher ABI. */
public class GameEvent {
    public static final class Emitter {
        public static final Emitter SELF = new Emitter();
        private Emitter() { }
    }
}
