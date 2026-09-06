package net.minecraft.server;

import java.util.List;

/** Server-list metadata value used by the 1.21.4 ping path. */
public class ServerMetadata {
    public static class Players {
        private final int max;
        private final int online;
        private final List<?> sample;
        public Players(int max, int online, List<?> sample) {
            this.max = Math.max(0, max);
            this.online = Math.max(0, online);
            this.sample = sample == null ? List.of() : List.copyOf(sample);
        }
        public int max() { return max; }
        public int online() { return online; }
        public List<?> sample() { return sample; }
        public int getMax() { return max; }
        public int getOnline() { return online; }
        public List<?> getSample() { return sample; }
    }
}
