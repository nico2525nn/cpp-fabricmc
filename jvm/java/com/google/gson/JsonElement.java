package com.google.gson;

/** Minimal Gson value used by the command argument serializer ABI. */
public class JsonElement {
    public String getAsString() { return toString(); }
    public int getAsInt() { return Integer.parseInt(getAsString()); }
    public boolean getAsBoolean() { return Boolean.parseBoolean(getAsString()); }
}
