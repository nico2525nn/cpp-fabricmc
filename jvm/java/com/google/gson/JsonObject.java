package com.google.gson;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/** Dependency-free subset of Gson's JsonObject used by Fabric serializers. */
public class JsonObject extends JsonElement {
    private final Map<String, JsonElement> values = new LinkedHashMap<>();

    public void add(String property, JsonElement value) { values.put(property, value == null ? new JsonPrimitive(null) : value); }
    public void addProperty(String property, String value) { add(property, new JsonPrimitive(value)); }
    public void addProperty(String property, Number value) { add(property, new JsonPrimitive(value)); }
    public void addProperty(String property, Boolean value) { add(property, new JsonPrimitive(value)); }
    public JsonElement get(String property) { return values.get(property); }
    public boolean has(String property) { return values.containsKey(property); }
    public JsonElement remove(String property) { return values.remove(property); }
    public Set<Map.Entry<String, JsonElement>> entrySet() { return java.util.Collections.unmodifiableSet(values.entrySet()); }
    public int size() { return values.size(); }
    @Override public String toString() {
        StringBuilder result = new StringBuilder("{");
        boolean first = true;
        for (Map.Entry<String, JsonElement> entry : values.entrySet()) {
            if (!first) result.append(',');
            first = false;
            result.append('"').append(entry.getKey().replace("\\", "\\\\").replace("\"", "\\\""))
                .append("\":").append(entry.getValue());
        }
        return result.append('}').toString();
    }
}
