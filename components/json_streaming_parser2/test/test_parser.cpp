#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <tuple>

#include "include/JsonStreamingParser2.h"
#include "include/JsonPathCallback.h"

// ── Parser event helpers ─────────────────────────────────────────

struct Event {
    int type;
    std::string key;
    std::string value;
};

std::vector<Event> events;

void eventCallback(JsonParseEvent evt) {
    Event e;
    e.type = evt.type;
    e.key = evt.key.value_or("");
    if (evt.value.has_value()) {
        auto& v = evt.value.value();
        if (v.isString()) e.value = '"' + v.getString() + '"';
        else if (v.isInt()) e.value = std::to_string(v.getInt());
        else if (v.isFloat()) e.value = std::to_string(v.getFloat());
        else if (v.isBool()) e.value = v.getBool() ? "true" : "false";
        else if (v.isNull()) e.value = "null";
    }
    events.push_back(e);
}

const char* eventName(int type) {
    switch (type) {
        case EVENT_START_DOCUMENT: return "START_DOCUMENT";
        case EVENT_END_DOCUMENT:   return "END_DOCUMENT";
        case EVENT_START_OBJECT:   return "START_OBJECT";
        case EVENT_END_OBJECT:     return "END_OBJECT";
        case EVENT_START_ARRAY:    return "START_ARRAY";
        case EVENT_END_ARRAY:      return "END_ARRAY";
        case EVENT_VALUE:          return "VALUE";
        default:                   return "UNKNOWN";
    }
}

int tests_run = 0;
int tests_passed = 0;

void dump_events() {
    printf("    Events:\n");
    for (auto& e : events) {
        printf("      %s", eventName(e.type));
        if (!e.key.empty()) printf(" key='%s'", e.key.c_str());
        if (!e.value.empty()) printf(" val=%s", e.value.c_str());
        printf("\n");
    }
}

bool check_events(std::vector<std::string> expected) {
    if (events.size() != expected.size()) {
        printf("    FAIL: expected %zu events, got %zu\n", expected.size(), events.size());
        dump_events();
        return false;
    }
    for (size_t i = 0; i < events.size(); i++) {
        std::string got = eventName(events[i].type);
        if (got != expected[i]) {
            printf("    FAIL: event[%zu] expected '%s', got '%s'\n", i, expected[i].c_str(), got.c_str());
            dump_events();
            return false;
        }
    }
    return true;
}

bool run_parser_test(const char* name, const char* json, std::vector<std::string> expected_events) {
    tests_run++;
    events.clear();
    JsonStreamingParser parser(eventCallback);
    
    for (const char* p = json; *p; p++) {
        parser.parse(*p);
        if (parser.hasParseError()) {
            printf("  FAIL: %s\n", name);
            printf("    Error at offset %d: %s\n", parser.getCharacterCount(), parser.getErrorMessage());
            printf("    JSON: %s\n", json);
            return false;
        }
    }
    
    if (!check_events(expected_events)) {
        printf("  FAIL: %s\n", name);
        return false;
    }
    
    printf("  OK: %s\n", name);
    tests_passed++;
    return true;
}

bool run_invalid_test(const char* name, const char* json) {
    tests_run++;
    events.clear();
    JsonStreamingParser parser(eventCallback);
    
    for (const char* p = json; *p; p++) {
        parser.parse(*p);
        if (parser.hasParseError()) {
            printf("  OK (expected error): %s\n", name);
            tests_passed++;
            return true;
        }
    }
    
    printf("  FAIL (expected error but none): %s\n", name);
    printf("    JSON: %s\n", json);
    return false;
}

// ── Test runner macro ────────────────────────────────────────────

#define CHECK(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { printf("  FAIL: %s\n", msg); return false; } \
    tests_passed++; \
} while(0)

static int section = 0;
void print_section(const char* name) {
    printf("\n--- %s ---\n\n", name);
}

// ── JsonPath tests ───────────────────────────────────────────────

bool test_jsonpath_self_equal() {
    auto p = JsonDoc;
    CHECK(p == p, "path should equal itself");
    printf("  OK: JsonPath self-equality\n");
    return true;
}

bool test_jsonpath_same_value_different_ptr() {
    // Two separately constructed paths with identical structure ARE equal
    // (operator== deep-compares the parent chain)
    JsonPath a = JsonDoc.withObject("key");
    JsonPath b = JsonDoc.withObject("key");
    CHECK(a == b, "structurally identical paths should be equal (deep compare)");
    printf("  OK: JsonPath structurally identical paths equal (deep compare)\n");
    return true;
}

bool test_jsonpath_different_type() {
    auto parent = JsonDoc;
    JsonPath obj = parent.withObject("k");
    JsonPath arr = parent.withArray("k");
    CHECK(!(obj == arr), "different type should not be equal");
    printf("  OK: JsonPath different type\n");
    return true;
}

bool test_jsonpath_different_key() {
    auto parent = JsonDoc;
    JsonPath a = parent.withObject("k1");
    JsonPath b = parent.withObject("k2");
    CHECK(!(a == b), "different key should not be equal");
    printf("  OK: JsonPath different key\n");
    return true;
}

bool test_jsonpath_no_key_vs_key() {
    auto parent = JsonDoc;
    JsonPath a = parent.withObject();
    JsonPath b = parent.withObject("k");
    CHECK(!(a == b), "path with no key should not equal path with key");
    printf("  OK: JsonPath no key vs key\n");
    return true;
}

bool test_jsonpath_chain_equality() {
    // Longer chains built independently are equal with deep comparison
    JsonPath a = JsonDoc.withObject("a").withArray("items");
    JsonPath b = JsonDoc.withObject("a").withArray("items");
    CHECK(a == b, "longer structural chains should be equal");
    printf("  OK: JsonPath longer chain equality (deep compare)\n");
    return true;
}

bool test_jsonpath_withObject() {
    JsonPath p = JsonDoc.withObject("key");
    CHECK(p.type == ELEM_OBJECT, "withObject should set type to ELEM_OBJECT");
    CHECK(p.key.has_value() && p.key.value() == "key", "withObject should set key");
    CHECK(p.parent != nullptr, "withObject should set parent");
    printf("  OK: JsonPath::withObject\n");
    return true;
}

bool test_jsonpath_withArray() {
    JsonPath p = JsonDoc.withArray();
    CHECK(p.type == ELEM_ARRAY, "withArray should set type to ELEM_ARRAY");
    CHECK(!p.key.has_value(), "withArray with no key should have no key");
    printf("  OK: JsonPath::withArray\n");
    return true;
}

bool test_jsonpath_toString() {
    // Build path: document → object("a") → array
    JsonPath p = JsonDoc.withObject("a").withArray();
    std::string s = p.toString();
    CHECK(s.find("<object>") != std::string::npos, "toString should contain <object>");
    CHECK(s.find("<array>") != std::string::npos, "toString should contain <array>");
    CHECK(s.find("a") != std::string::npos, "toString should contain key 'a'");
    printf("  OK: JsonPath::toString\n");
    return true;
}

bool test_jsonpath_toString_document() {
    std::string s = JsonDoc.toString();
    CHECK(s.empty() || s == "<object>" || s.find("ument") != std::string::npos,
          "document toString should not crash");
    printf("  OK: JsonPath::toString (document)\n");
    return true;
}

// ── JsonPathHandler integration tests ────────────────────────────

struct PathEvent {
    int type;
    std::string path;
    std::string key;
    std::string value;
};

std::vector<PathEvent> pathEvents;

void pathEventCallback(JsonPathEvent evt) {
    PathEvent e;
    e.type = evt.type;
    e.path = evt.path.toString();
    e.key = evt.key.value_or("");
    if (evt.value.has_value()) {
        auto& v = evt.value.value();
        if (v.isString()) e.value = '"' + v.getString() + '"';
        else if (v.isInt()) e.value = std::to_string(v.getInt());
        else if (v.isFloat()) e.value = std::to_string(v.getFloat());
        else if (v.isBool()) e.value = v.getBool() ? "true" : "false";
        else if (v.isNull()) e.value = "null";
    }
    pathEvents.push_back(e);
}

void dump_path_events() {
    for (auto& e : pathEvents) {
        printf("      %s path='%s'", eventName(e.type), e.path.c_str());
        if (!e.key.empty()) printf(" key='%s'", e.key.c_str());
        if (!e.value.empty()) printf(" val=%s", e.value.c_str());
        printf("\n");
    }
}

bool run_path_test(const char* name, const char* json,
                   std::vector<std::pair<int, std::string>> expected) {
    tests_run++;
    pathEvents.clear();
    JsonPathEventCB_t pathCb = pathEventCallback;
    JsonStreamingParser parser(JsonPathHandler(pathCb));
    
    for (const char* p = json; *p; p++) {
        parser.parse(*p);
        if (parser.hasParseError()) {
            printf("  FAIL: %s\n", name);
            printf("    Error at offset %d: %s\n", parser.getCharacterCount(), parser.getErrorMessage());
            return false;
        }
    }
    
    if (pathEvents.size() != expected.size()) {
        printf("  FAIL: %s\n", name);
        printf("    Expected %zu path events, got %zu\n", expected.size(), pathEvents.size());
        dump_path_events();
        return false;
    }
    
    for (size_t i = 0; i < pathEvents.size(); i++) {
        if (pathEvents[i].type != expected[i].first ||
            pathEvents[i].path != expected[i].second) {
            printf("  FAIL: %s\n", name);
            printf("    Event[%zu]: expected (%s, '%s'), got (%s, '%s')\n",
                   i, eventName(expected[i].first), expected[i].second.c_str(),
                   eventName(pathEvents[i].type), pathEvents[i].path.c_str());
            dump_path_events();
            return false;
        }
    }
    
    printf("  OK: %s\n", name);
    tests_passed++;
    return true;
}

bool run_path_value_test(const char* name, const char* json,
                         std::vector<std::tuple<int, std::string, std::string>> expected) {
    tests_run++;
    pathEvents.clear();
    JsonPathEventCB_t pathCb = pathEventCallback;
    JsonStreamingParser parser(JsonPathHandler(pathCb));
    
    for (const char* p = json; *p; p++) {
        parser.parse(*p);
        if (parser.hasParseError()) {
            printf("  FAIL: %s\n", name);
            printf("    Error at offset %d: %s\n", parser.getCharacterCount(), parser.getErrorMessage());
            return false;
        }
    }
    
    if (pathEvents.size() != expected.size()) {
        printf("  FAIL: %s\n", name);
        printf("    Expected %zu events, got %zu\n", expected.size(), pathEvents.size());
        dump_path_events();
        return false;
    }
    
    for (size_t i = 0; i < pathEvents.size(); i++) {
        int expType = std::get<0>(expected[i]);
        const std::string& expPath = std::get<1>(expected[i]);
        const std::string& expVal = std::get<2>(expected[i]);
        if (pathEvents[i].type != expType ||
            pathEvents[i].path != expPath ||
            pathEvents[i].value != expVal) {
            printf("  FAIL: %s\n", name);
            printf("    Event[%zu]: expected (%s, '%s', '%s'), got (%s, '%s', '%s')\n",
                   i, eventName(expType), expPath.c_str(), expVal.c_str(),
                   eventName(pathEvents[i].type), pathEvents[i].path.c_str(),
                   pathEvents[i].value.c_str());
            dump_path_events();
            return false;
        }
    }
    
    printf("  OK: %s\n", name);
    tests_passed++;
    return true;
}

// ── Main ─────────────────────────────────────────────────────────

int main() {
    printf("=== JsonStreamingParser Tests ===\n\n");

    section++;
    print_section("Parser: basic JSON parsing");
    run_parser_test("array of objects",
        R"([ { "key": "value" }, { "key": "value" } ])",
        {"START_DOCUMENT", "START_ARRAY", "START_OBJECT", "VALUE", "END_OBJECT",
         "START_OBJECT", "VALUE", "END_OBJECT", "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("single object",
        R"({ "a": 1 })",
        {"START_DOCUMENT", "START_OBJECT", "VALUE", "END_OBJECT", "END_DOCUMENT"});
    run_parser_test("empty array",
        R"([])",
        {"START_DOCUMENT", "START_ARRAY", "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("empty object",
        R"({})",
        {"START_DOCUMENT", "START_OBJECT", "END_OBJECT", "END_DOCUMENT"});
    run_parser_test("nested object",
        R"({ "a": { "b": "c" } })",
        {"START_DOCUMENT", "START_OBJECT", "START_OBJECT", "VALUE", "END_OBJECT", "END_OBJECT", "END_DOCUMENT"});
    run_parser_test("array of strings",
        R"(["a", "b", "c"])",
        {"START_DOCUMENT", "START_ARRAY", "VALUE", "VALUE", "VALUE", "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("nested arrays",
        R"([[1, 2], [3, 4]])",
        {"START_DOCUMENT", "START_ARRAY", "START_ARRAY", "VALUE", "VALUE", "END_ARRAY",
         "START_ARRAY", "VALUE", "VALUE", "END_ARRAY", "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("deeply nested array of objects (no spaces)",
        R"([{"a":1},{"a":2},{"a":3},{"a":4},{"a":5}])",
        {"START_DOCUMENT", "START_ARRAY",
         "START_OBJECT", "VALUE", "END_OBJECT",
         "START_OBJECT", "VALUE", "END_OBJECT",
         "START_OBJECT", "VALUE", "END_OBJECT",
         "START_OBJECT", "VALUE", "END_OBJECT",
         "START_OBJECT", "VALUE", "END_OBJECT",
         "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("mixed types",
        R"({ "s": "hello", "n": 42, "f": 3.14, "b": true, "x": null })",
        {"START_DOCUMENT", "START_OBJECT",
         "VALUE", "VALUE", "VALUE", "VALUE", "VALUE",
         "END_OBJECT", "END_DOCUMENT"});
    run_parser_test("array of primitives",
        R"([true, false, null, 42, "hello"])",
        {"START_DOCUMENT", "START_ARRAY",
         "VALUE", "VALUE", "VALUE", "VALUE", "VALUE",
         "END_ARRAY", "END_DOCUMENT"});

    print_section("Parser: leading whitespace");
    run_parser_test("leading space before object",
        R"( { "a": 1 })",
        {"START_DOCUMENT", "START_OBJECT", "VALUE", "END_OBJECT", "END_DOCUMENT"});
    run_parser_test("leading tab before array",
        R"(	["a", "b"])",
        {"START_DOCUMENT", "START_ARRAY", "VALUE", "VALUE", "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("leading newline before object",
        R"(
{"x": 42})",
        {"START_DOCUMENT", "START_OBJECT", "VALUE", "END_OBJECT", "END_DOCUMENT"});
    run_parser_test("leading CR before array",
        R"(
[1, 2, 3])",
        {"START_DOCUMENT", "START_ARRAY", "VALUE", "VALUE", "VALUE", "END_ARRAY", "END_DOCUMENT"});
    run_parser_test("multiple leading whitespace",
        R"(  	 
 
{ "a": true })",
        {"START_DOCUMENT", "START_OBJECT", "VALUE", "END_OBJECT", "END_DOCUMENT"});

    print_section("Parser: invalid JSON");
    run_parser_test("trailing comma in array (lenient)",
        R"([1,])",
        {"START_DOCUMENT", "START_ARRAY", "VALUE", "END_ARRAY", "END_DOCUMENT"});
    run_invalid_test("missing colon", R"({ "a" 1 })");
    run_invalid_test("invalid value", R"([hello])");

    print_section("JsonPath: equality");
    test_jsonpath_self_equal();
    test_jsonpath_same_value_different_ptr();
    test_jsonpath_different_type();
    test_jsonpath_different_key();
    test_jsonpath_no_key_vs_key();
    test_jsonpath_chain_equality();

    print_section("JsonPath: construction / toString");
    test_jsonpath_withObject();
    test_jsonpath_withArray();
    test_jsonpath_toString();
    test_jsonpath_toString_document();

    print_section("JsonPathHandler: integration");
    run_path_test("simple object path",
        R"({ "a": 1 })",
        {{EVENT_START_DOCUMENT, ""},
         {EVENT_START_OBJECT,  "/<object>"},
         {EVENT_VALUE,         "/<object>"},
         {EVENT_END_OBJECT,    "/<object>"},
         {EVENT_END_DOCUMENT,  ""}});

    run_path_test("nested object path",
        R"({ "a": { "b": "c" } })",
        {{EVENT_START_DOCUMENT, ""},
         {EVENT_START_OBJECT,  "/<object>"},
         {EVENT_START_OBJECT,  "/<object>/a <object>"},
         {EVENT_VALUE,         "/<object>/a <object>"},
         {EVENT_END_OBJECT,    "/<object>/a <object>"},
         {EVENT_END_OBJECT,    "/<object>"},
         {EVENT_END_DOCUMENT,  ""}});

    run_path_test("array path",
        R"([1, 2])",
        {{EVENT_START_DOCUMENT, ""},
         {EVENT_START_ARRAY,   "/<array>"},
         {EVENT_VALUE,         "/<array>"},
         {EVENT_VALUE,         "/<array>"},
         {EVENT_END_ARRAY,     "/<array>"},
         {EVENT_END_DOCUMENT,  ""}});

    run_path_test("array of objects path",
        R"([{"a":1},{"a":2}])",
        {{EVENT_START_DOCUMENT, ""},
         {EVENT_START_ARRAY,   "/<array>"},
         {EVENT_START_OBJECT,  "/<array>/<object>"},
         {EVENT_VALUE,         "/<array>/<object>"},
         {EVENT_END_OBJECT,    "/<array>/<object>"},
         {EVENT_START_OBJECT,  "/<array>/<object>"},
         {EVENT_VALUE,         "/<array>/<object>"},
         {EVENT_END_OBJECT,    "/<array>/<object>"},
         {EVENT_END_ARRAY,     "/<array>"},
         {EVENT_END_DOCUMENT,  ""}});

    run_path_test("nested mixed path",
        R"({ "items": [ { "x": 10 } ] })",
        {{EVENT_START_DOCUMENT, ""},
         {EVENT_START_OBJECT,  "/<object>"},
         {EVENT_START_ARRAY,   "/<object>/items <array>"},
         {EVENT_START_OBJECT,  "/<object>/items <array>/<object>"},
         {EVENT_VALUE,         "/<object>/items <array>/<object>"},
         {EVENT_END_OBJECT,    "/<object>/items <array>/<object>"},
         {EVENT_END_ARRAY,     "/<object>/items <array>"},
         {EVENT_END_OBJECT,    "/<object>"},
         {EVENT_END_DOCUMENT,  ""}});

    print_section("JsonPathHandler: values with paths");
    run_path_value_test("keyed object values",
        R"({ "x": 42, "y": "hi" })",
        {{EVENT_START_DOCUMENT, "", ""},
         {EVENT_START_OBJECT,  "/<object>", ""},
         {EVENT_VALUE,         "/<object>", "42"},
         {EVENT_VALUE,         "/<object>", "\"hi\""},
         {EVENT_END_OBJECT,    "/<object>", ""},
         {EVENT_END_DOCUMENT,  "", ""}});

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
