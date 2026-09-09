package com.google.gson;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;

/** Minimal Gson array tree used by resource metadata parsing. */
public final class JsonArray extends JsonElement implements Iterable<JsonElement> {
    private final List<JsonElement> values = new ArrayList<>();

    public void add(JsonElement value) { values.add(value == null ? new JsonPrimitive(null) : value); }
    public int size() { return values.size(); }
    public JsonElement get(int index) { return values.get(index); }
    public List<JsonElement> asList() { return Collections.unmodifiableList(values); }
    @Override public Iterator<JsonElement> iterator() { return asList().iterator(); }
    @Override public String toString() { return values.toString(); }
}
