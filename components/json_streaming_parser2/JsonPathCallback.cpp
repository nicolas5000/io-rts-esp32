#include "JsonPathCallback.h"

JsonParserCB_t JsonPathHandler(JsonPathEventCB_t cb) {
    struct Ctx {
        std::shared_ptr<JsonPath> current;
        std::vector<std::optional<int>> arrayIndexStack;

        std::optional<int> getAndIncrementIndex() {
            auto& maybeIndex = arrayIndexStack.back();
            if (maybeIndex) return (*maybeIndex)++;
            return {};
        }
        std::optional<int> getPreviousIndex() {
            auto& maybeIndex = arrayIndexStack.back();
            if (maybeIndex) return (*maybeIndex)-1;
            return {};
        }
    };
    auto ctx = std::make_shared<Ctx>();
    ctx->current = std::make_shared<JsonPath>(JsonDoc);
    ctx->arrayIndexStack.push_back({});
    return [cb = std::move(cb), ctx](JsonParseEvent evt) mutable {
        switch(evt.type) {
            case EVENT_START_DOCUMENT: cb({ evt.type, *ctx->current, {}, {}, {} }); break;
            case EVENT_END_DOCUMENT: cb({ evt.type, *ctx->current, {}, {}, {} }); break;
            case EVENT_START_ARRAY: {
                auto index = ctx->getAndIncrementIndex();
                ctx->current = std::make_shared<JsonPath>(JsonPath{ ELEM_ARRAY, ctx->current, evt.key, index });
                cb({ evt.type, *ctx->current, evt.key, {}, index });
                ctx->arrayIndexStack.push_back(0);
                break;
            }
            case EVENT_END_ARRAY: {
                ctx->arrayIndexStack.pop_back();
                cb({ evt.type, *ctx->current, evt.key, {}, ctx->getPreviousIndex() });
                ctx->current = ctx->current->parent;
                break;
            }
            case EVENT_START_OBJECT: {
                auto index = ctx->getAndIncrementIndex();
                ctx->current = std::make_shared<JsonPath>(JsonPath{ ELEM_OBJECT, ctx->current, evt.key, index });
                cb({ evt.type, *ctx->current, evt.key, {}, index });
                ctx->arrayIndexStack.push_back({});
                break;
            }
            case EVENT_END_OBJECT:
                ctx->arrayIndexStack.pop_back();
                cb({ evt.type, *ctx->current, evt.key, {}, ctx->getPreviousIndex() });
                ctx->current = ctx->current->parent;
                break;
            case EVENT_VALUE: {
               cb({ evt.type, *ctx->current, evt.key, evt.value, ctx->getAndIncrementIndex() });
                break;
            }
            case EVENT_WHITESPACE: cb({ evt.type, *ctx->current, evt.key, {}, ctx->arrayIndexStack.back() }); break;
        }
    };
}
