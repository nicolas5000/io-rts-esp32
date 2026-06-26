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

struct JsonPath {
    int type;
    std::shared_ptr<JsonPath> parent;
    std::optional<std::string> key;

    bool operator==(const JsonPath& other) const {
        return type == other.type 
            && key == other.key
            && (parent == other.parent || (parent && other.parent && *parent == *other.parent));
    }

    JsonPath withObject() const {
        return { ELEM_OBJECT, std::make_shared<JsonPath>(*this), {} };
    }

    JsonPath withObject(std::string key) const {
        return { ELEM_OBJECT, std::make_shared<JsonPath>(*this), key };
    }
 
    JsonPath withArray() const {
        return { ELEM_ARRAY, std::make_shared<JsonPath>(*this), {} };
    }

    JsonPath withArray(std::string key) const {
        return { ELEM_ARRAY, std::make_shared<JsonPath>(*this), key };
    }

    std::string toString() const {
        std::string typeStr;
        switch(type) {
            case ELEM_DOCUMENT: typeStr = ""; break;
            case ELEM_ARRAY: typeStr = "<array>"; break;
            case ELEM_OBJECT: typeStr = "<object>"; break;
        }
        std::string thisStr = key.has_value() ? key.value() + " " + typeStr : typeStr;
        return parent == nullptr ? thisStr : parent.get()->toString() + "/" + thisStr;
    }
};

const JsonPath JsonDoc = { ELEM_DOCUMENT, nullptr, {} };

struct JsonPathEvent {
    int type;
    JsonPath path;
    std::optional<std::string> key;
    std::optional<ElementValue> value;
};

typedef std::function<void(JsonPathEvent)> JsonPathEventCB_t;

extern JsonParserCB_t JsonPathHandler(JsonPathEventCB_t& cb) {    
    auto current = std::make_shared<JsonPath>(JsonDoc);
    return [&cb, current](JsonParseEvent evt) mutable {
        switch(evt.type) {
            case EVENT_START_DOCUMENT: cb({ evt.type, *current, {}, {} }); break;
            case EVENT_END_DOCUMENT: cb({ evt.type, *current, {}, {} }); break;
            case EVENT_START_ARRAY: 
                current = std::make_shared<JsonPath>(JsonPath{ELEM_ARRAY, current, evt.key});
                cb({ evt.type, *current, evt.key, {} });
                break;
            case EVENT_END_ARRAY: 
                cb({ evt.type, *current, evt.key, {} });
                current = current->parent;
                break;
            case EVENT_START_OBJECT: 
                current = std::make_shared<JsonPath>(JsonPath{ELEM_OBJECT, current, evt.key});
                cb({ evt.type, *current, evt.key, {} });
                break;
            case EVENT_END_OBJECT: 
                cb({ evt.type, *current, evt.key, {} });
                current = current->parent;
                break;
            case EVENT_VALUE: cb({ evt.type, *current, evt.key, evt.value }); break;
            case EVENT_WHITESPACE: cb({ evt.type, *current, evt.key, {} }); break;
        }
    };
}

#endif // JSON_PATH_CALLBACK_H