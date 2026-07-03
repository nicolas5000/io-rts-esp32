#ifndef JSON_PATH_CALLBACK_H
#define JSON_PATH_CALLBACK_H

#include <string>
#include <functional>
#include <optional>
#include <memory>

#include "ElementValue.h"
#include "JsonStreamingParser2.h"

#define ELEM_DOCUMENT   40
#define ELEM_OBJECT     41
#define ELEM_ARRAY      42
#define ELEM_INVALID    43

struct JsonPath {
    int type;
    std::shared_ptr<JsonPath> parent;
    std::optional<std::string> key;
    std::optional<int> index;

    bool operator==(const JsonPath& other) const {
        return type == other.type 
            && key == other.key
            && (!index.has_value() || !other.index.has_value() || *index == *other.index)
            && (parent == other.parent || (parent && other.parent && *parent == *other.parent));
    }

    bool startsWith(const JsonPath& other) const {
        return *this == other || (parent != nullptr && *parent == other);
    }

    bool endsWith(const JsonPath& other) const {
        return type == other.type 
            && key == other.key
            && index == other.index
            && (other.parent == nullptr || (parent != nullptr && parent->endsWith(*other.parent)));
    }

    bool contains(const JsonPath& other) const {
        return endsWith(other) || (parent != nullptr && parent->endsWith(other));
    }

    int depth() const {
        return 1 + (parent ? parent->depth() : 0);
    }

    JsonPath withObject() const {
        return { ELEM_OBJECT, std::make_shared<JsonPath>(*this), {}, {} };
    }

    JsonPath withObject(int index) const {
        if (type != ELEM_ARRAY) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), key, index };
        return { ELEM_OBJECT, std::make_shared<JsonPath>(*this), {}, index };
    }

    JsonPath withObject(std::string key) const {
        if (type == ELEM_ARRAY) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), key, {} };
        return { ELEM_OBJECT, std::make_shared<JsonPath>(*this), key, {} };
    }

    JsonPath withObject(std::string key, int index) const {
        if (type == ELEM_ARRAY) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), key, index };
        return { ELEM_OBJECT, std::make_shared<JsonPath>(*this), key, index };
    }
 
    JsonPath withArray() const {
        if (type == ELEM_OBJECT) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), {}, {} };
        return { ELEM_ARRAY, std::make_shared<JsonPath>(*this), {}, {} };
    }

    JsonPath withArray(int index) const {
        if (type == ELEM_OBJECT) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), {}, index };
        return { ELEM_ARRAY, std::make_shared<JsonPath>(*this), {}, index };
    }

    JsonPath withArray(std::string key) const {
        if (type == ELEM_ARRAY) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), key, {} };
        return { ELEM_ARRAY, std::make_shared<JsonPath>(*this), key, {} };
    }

    JsonPath withArray(std::string key, int index) const {
        if (type == ELEM_ARRAY) return { ELEM_INVALID, std::make_shared<JsonPath>(*this), key, index };
        return { ELEM_ARRAY, std::make_shared<JsonPath>(*this), key, index };
    }

    std::string toString() const {
        std::string typeStr;
        switch(type) {
            case ELEM_DOCUMENT: typeStr = ""; break;
            case ELEM_ARRAY: typeStr = "<array>"; break;
            case ELEM_OBJECT: typeStr = "<object>"; break;
            case ELEM_INVALID: typeStr = "<INVALID!>"; break;
        }
        std::string thisStr = (key.has_value() ? *key + " " + typeStr : typeStr) + (index.has_value() ? std::string("[") + std::to_string(*index) + "]" : "");
        return parent == nullptr ? thisStr : parent.get()->toString() + "/" + thisStr;
    }
};

const JsonPath JsonDoc = { ELEM_DOCUMENT, nullptr, {}, {} };
const JsonPath JsonObject = { ELEM_OBJECT, nullptr, {}, {} };
const JsonPath JsonArray = { ELEM_ARRAY, nullptr, {}, {} };

struct JsonPathEvent {
    int type;
    JsonPath path;
    std::optional<std::string> key;
    std::optional<ElementValue> value;
    std::optional<int> index; // only applicable for type == ELEM_ARRAY, holds the index of the current element in the array
};

typedef std::function<void(JsonPathEvent)> JsonPathEventCB_t;

JsonParserCB_t JsonPathHandler(JsonPathEventCB_t cb);

#endif // JSON_PATH_CALLBACK_H