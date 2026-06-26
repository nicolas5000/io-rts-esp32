// Original code: https://github.com/mrcodetastic/json-streaming-parser2
// Adapted to our own needs

#ifndef JSON_STREAMING_PARSER2_H
#define JSON_STREAMING_PARSER2_H

#include <functional>
#include <string>
#include <optional>

#include "ElementValue.h"

#define STATE_START_DOCUMENT     0
#define STATE_DONE               -1
#define STATE_IN_ARRAY           1
#define STATE_IN_OBJECT          2
#define STATE_END_KEY            3
#define STATE_AFTER_KEY          4
#define STATE_IN_STRING          5
#define STATE_START_ESCAPE       6
#define STATE_UNICODE            7
#define STATE_IN_NUMBER          8
#define STATE_IN_TRUE            9
#define STATE_IN_FALSE           10
#define STATE_IN_NULL            11
#define STATE_AFTER_VALUE        12
#define STATE_UNICODE_SURROGATE  13

#define STACK_OBJECT             0
#define STACK_ARRAY              1
#define STACK_KEY                2
#define STACK_STRING             3

#ifndef JSON_PARSER_BUFFER_MAX_LENGTH
#define JSON_PARSER_BUFFER_MAX_LENGTH  256
#endif

#ifndef JSON_PARSER_STACK_MAX_DEPTH
#define JSON_PARSER_STACK_MAX_DEPTH    20
#endif

#ifndef JSON_PARSER_PATH_MAX_DEPTH
#define JSON_PARSER_PATH_MAX_DEPTH     20
#endif

#define EVENT_START_DOCUMENT    20
#define EVENT_END_DOCUMENT      21
#define EVENT_START_OBJECT      22
#define EVENT_END_OBJECT        23
#define EVENT_START_ARRAY       24
#define EVENT_END_ARRAY         25
#define EVENT_VALUE             26
#define EVENT_WHITESPACE        27

struct JsonParseEvent {
  int type;
  std::optional<std::string> key;
  std::optional<ElementValue> value;
};

typedef std::function<void(JsonParseEvent)> JsonParserCB_t;

class JsonStreamingParser {
  private:

    int state;
    int stack[JSON_PARSER_STACK_MAX_DEPTH];
    int stackPos = 0;
    
    ElementValue elementValue;
    
    JsonParserCB_t cbFun;

    bool doEmitWhitespace = false;
	
    // fixed length buffer array to prepare for c code
    char buffer[JSON_PARSER_BUFFER_MAX_LENGTH];
    int bufferPos = 0;

    char unicodeEscapeBuffer[10];
    int unicodeEscapeBufferPos = 0;

    char unicodeBuffer[10];
    int unicodeBufferPos = 0;

    int characterCounter = 0;

    int unicodeHighSurrogate = 0;

    std::optional<std::string> key;

    // Error handling
    bool hasError = false;
    const char* errorMessage = nullptr;

    void increaseBufferPointer();

    void endString();

    void endArray();

    void startValue(char c);

    void startKey();

    void processEscapeCharacters(char c);

    bool isDigit(char c);

    bool isHexCharacter(char c);

    char convertCodepointToCharacter(int num);

    void endUnicodeCharacter(int codepoint);

    void startNumber(char c);

    void startString();

    void startObject();

    void startArray();

    void endNull();

    void endFalse();

    void endTrue();

    void endDocument();

    int convertDecimalBufferToInt(char myArray[], int length);

    void endNumber();

    void endUnicodeSurrogateInterstitial();

    bool doesCharArrayContain(char myArray[], int length, char c);

    int getHexArrayAsDecimal(char hexArray[], int length);

    void processUnicodeCharacter(char c);

    void endObject();

  public:
    JsonStreamingParser(const JsonParserCB_t cb);
    void parse(char c);
    void reset();
    
    // Error handling methods
    bool hasParseError() const { return hasError; }
    const char* getErrorMessage() const { return errorMessage; }
    void clearError() { hasError = false; errorMessage = nullptr; }
    
    // Buffer status methods
    int getBufferPosition() const { return bufferPos; }
    int getMaxBufferSize() const { return JSON_PARSER_BUFFER_MAX_LENGTH; }
    int getStackDepth() const { return stackPos; }
    int getCharacterCount() const { return characterCounter; }
};

#endif // JSON_STREAMING_PARSER2_H
