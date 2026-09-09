package com.google.gson;

/** Small primitive JSON value for serializers that only need scalar fields. */
public final class JsonPrimitive extends JsonElement {
    private final Object value;
    public JsonPrimitive(Object value) { this.value = value; }
    @Override public String getAsString() { return String.valueOf(value); }
    @Override public String toString() {
        if (value == null) return "null";
        if (value instanceof Number || value instanceof Boolean) return getAsString();
        return "\"" + getAsString().replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
