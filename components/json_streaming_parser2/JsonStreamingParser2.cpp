#include "JsonStreamingParser2.h"

JsonStreamingParser::JsonStreamingParser(JsonParserCB_t cb)
{
  cbFun = cb;
  reset();
}

void JsonStreamingParser::reset()
{
  state = STATE_START_DOCUMENT;
  bufferPos = 0;
  unicodeEscapeBufferPos = 0;
  unicodeBufferPos = 0;
  characterCounter = 0;
  stackPos = 0;
  for (int i = 0; i < JSON_PARSER_STACK_MAX_DEPTH; i++) stack[i] = -1;
  hasError = false;
  errorMessage = nullptr;
}

void JsonStreamingParser::parse(char c)
{
  // Early return if we have an error
  if (hasError)
  {
    return;
  }

  // Check for stack overflow
  if (stackPos >= JSON_PARSER_STACK_MAX_DEPTH - 1)
  {
    hasError = true;
    errorMessage = "Stack overflow - JSON too deeply nested";
    return;
  }

  // valid whitespace characters in JSON (from RFC4627 for JSON) include:
  // space, horizontal tab, line feed or new line, and carriage return.
  // thanks:
  // http://stackoverflow.com/questions/16042274/definition-of-whitespace-in-json
  if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && !(state == STATE_IN_STRING || state == STATE_UNICODE || state == STATE_START_ESCAPE || state == STATE_IN_NUMBER))
  {
    return;
  }
  switch (state)
  {
  case STATE_IN_STRING:
    if (c == stringDelimiter)
    {
      endString();
    }
    else if (c == '\\')
    {
      state = STATE_START_ESCAPE;
    }
    else if ((c < 0x1f) || (c == 0x7f))
    {
      hasError = true;
      errorMessage = "Unescaped control character in string";
      return;
    }
    else
    {
      if (hasError)
        return; // Check if buffer overflow occurred
      buffer[bufferPos] = c;
      increaseBufferPointer();
    }
    break;
  case STATE_IN_ARRAY:
    if (c == ']')
    {
      endArray();
    }
    else
    {
      startValue(c);
    }
    break;
  case STATE_IN_OBJECT:
    if (c == '}')
    {
      endObject();
    }
    else if (c == '"' || c == '\'')
    {
      startKey(c);
    }
    else
    {
      hasError = true;
      errorMessage = "Expected start of string for object key";
      return;
    }
    break;
  case STATE_END_KEY:
    if (c != ':')
    {
      hasError = true;
      errorMessage = "Expected ':' after object key";
      return;
    }
    state = STATE_AFTER_KEY;
    break;
  case STATE_AFTER_KEY:
    startValue(c);
    break;
  case STATE_START_ESCAPE:
    processEscapeCharacters(c);
    break;
  case STATE_UNICODE:
    processUnicodeCharacter(c);
    break;
  case STATE_UNICODE_SURROGATE:
    unicodeEscapeBuffer[unicodeEscapeBufferPos] = c;
    unicodeEscapeBufferPos++;
    if (unicodeEscapeBufferPos == 2)
    {
      endUnicodeSurrogateInterstitial();
    }
    break;
  case STATE_AFTER_VALUE:
  {
    // not safe for size == 0!!!
    int within = stack[stackPos - 1];
    if (within == STACK_OBJECT)
    {
      if (c == '}')
      {
        endObject();
      }
      else if (c == ',')
      {
        state = STATE_IN_OBJECT;
      }
      else
      {
        hasError = true;
        errorMessage = "Expected ',' or '}' while parsing object";
        return;
      }
    }
    else if (within == STACK_ARRAY)
    {
      if (c == ']')
      {
        endArray();
      }
      else if (c == ',')
      {
        state = STATE_IN_ARRAY;
      }
      else
      {
        hasError = true;
        errorMessage = "Expected ',' or ']' while parsing array";
        return;
      }
    }
    else
    {
      hasError = true;
      errorMessage = "Finished literal but unclear what state to move to";
      return;
    }
  }
  break;
  case STATE_IN_NUMBER:
    if (c >= '0' && c <= '9')
    {
      buffer[bufferPos] = c;
      increaseBufferPointer();
    }
    else if (c == '.')
    {
      if (doesCharArrayContain(buffer, bufferPos, '.'))
      {
        hasError = true;
        errorMessage = "Cannot have multiple decimal points in number";
        return;
      }
      else if (doesCharArrayContain(buffer, bufferPos, 'e'))
      {
        hasError = true;
        errorMessage = "Cannot have decimal point in exponent";
        return;
      }
      buffer[bufferPos] = c;
      increaseBufferPointer();
    }
    else if (c == 'e' || c == 'E')
    {
      if (doesCharArrayContain(buffer, bufferPos, 'e'))
      {
        hasError = true;
        errorMessage = "Cannot have multiple exponents in number";
        return;
      }
      buffer[bufferPos] = c;
      increaseBufferPointer();
    }
    else if (c == '+' || c == '-')
    {
      if (bufferPos <= 0)
      {
        hasError = true;
        errorMessage = "Invalid number format";
        return;
      }
      char last = buffer[bufferPos - 1];
      if (!(last == 'e' || last == 'E'))
      {
        hasError = true;
        errorMessage = "Can only have '+' or '-' after 'e' or 'E' in number";
        return;
      }
      buffer[bufferPos] = c;
      increaseBufferPointer();
    }
    else
    {
      endNumber();
      // we have consumed one beyond the end of the number
      parse(c);
    }
    break;
  case STATE_IN_TRUE:
    buffer[bufferPos] = c;
    increaseBufferPointer();
    if (bufferPos == 4)
    {
      endTrue();
    }
    break;
  case STATE_IN_FALSE:
    buffer[bufferPos] = c;
    increaseBufferPointer();
    if (bufferPos == 5)
    {
      endFalse();
    }
    break;
  case STATE_IN_NULL:
    buffer[bufferPos] = c;
    increaseBufferPointer();
    if (bufferPos == 4)
    {
      endNull();
    }
    break;
  case STATE_START_DOCUMENT:
    cbFun({EVENT_START_DOCUMENT, {}, {}});
    if (c == '[')
    {
      startArray();
    }
    else if (c == '{')
    {
      startObject();
    }
    else
    {
      hasError = true;
      errorMessage = "Document must start with object or array";
      return;
    }
    break;
    // case STATE_DONE:
    //  throw new ParsingError($this->_line_number, $this->_char_number,
    //  "Expected end of document.");
    // default:
    //  throw new ParsingError($this->_line_number, $this->_char_number,
    //  "Internal error. Reached an unknown state: ".$this->_state);
  }
  characterCounter++;
}

void JsonStreamingParser::increaseBufferPointer()
{
  if (bufferPos >= JSON_PARSER_BUFFER_MAX_LENGTH - 1)
  {
    hasError = true;
    errorMessage = "Buffer overflow - JSON string/number too long";
    return;
  }
  bufferPos++;
}

void JsonStreamingParser::endString()
{
  int popped = stack[stackPos - 1];
  stackPos--;
  if (popped == STACK_KEY)
  {
    buffer[bufferPos] = '\0';
    state = STATE_END_KEY;
    key = std::string(buffer);
  }
  else if (popped == STACK_STRING)
  {
    buffer[bufferPos] = '\0';
    cbFun({EVENT_VALUE, key, elementValue.with(buffer)});
    key = {};
    state = STATE_AFTER_VALUE;
  }
  else
  {
    // throw new ParsingError($this->_line_number, $this->_char_number,
    // "Unexpected end of string.");
  }
  bufferPos = 0;
  stringDelimiter = 0;
}

void JsonStreamingParser::startValue(char c)
{
  if (c == '[')
  {
    startArray();
  }
  else if (c == '{')
  {
    startObject();
  }
  else if (c == '"' || c == '\'')
  {
    startString(c);
  }
  else if (isDigit(c))
  {
    startNumber(c);
  }
  else if (c == 't')
  {
    state = STATE_IN_TRUE;
    buffer[bufferPos] = c;
    increaseBufferPointer();
  }
  else if (c == 'f')
  {
    state = STATE_IN_FALSE;
    buffer[bufferPos] = c;
    increaseBufferPointer();
  }
  else if (c == 'n')
  {
    state = STATE_IN_NULL;
    buffer[bufferPos] = c;
    increaseBufferPointer();
  }
  else
  {
    hasError = true;
    errorMessage = "Unexpected character for value";
    return;
  }
}

bool JsonStreamingParser::isDigit(char c)
{
  // Only concerned with the first character in a number.
  return (c >= '0' && c <= '9') || c == '-';
}

void JsonStreamingParser::endArray()
{
  if (stackPos <= 0)
  {
    hasError = true;
    errorMessage = "Unexpected end of array - stack underflow";
    return;
  }
  int popped = stack[stackPos - 1];
  stackPos--;
  if (popped != STACK_ARRAY)
  {
    hasError = true;
    errorMessage = "Unexpected end of array encountered";
    return;
  }
  cbFun({EVENT_END_ARRAY, {}, {}});
  state = STATE_AFTER_VALUE;
  if (stackPos == 0)
  {
    endDocument();
  }
}

void JsonStreamingParser::startKey(char c)
{
  stack[stackPos] = STACK_KEY;
  stackPos++;
  state = STATE_IN_STRING;
  stringDelimiter = c;
}

void JsonStreamingParser::endObject()
{
  if (stackPos <= 0)
  {
    hasError = true;
    errorMessage = "Unexpected end of object - stack underflow";
    return;
  }
  int popped = stack[stackPos - 1];
  stackPos--;
  if (popped != STACK_OBJECT)
  {
    hasError = true;
    errorMessage = "Unexpected end of object encountered";
    return;
  }
  cbFun({EVENT_END_OBJECT, {}, {}});
  state = STATE_AFTER_VALUE;
  if (stackPos == 0)
  {
    endDocument();
  }
}

void JsonStreamingParser::processEscapeCharacters(char c)
{
  if (c == stringDelimiter)
  {
    buffer[bufferPos] = stringDelimiter;
    increaseBufferPointer();
  }
  else if (c == '\\')
  {
    buffer[bufferPos] = '\\';
    increaseBufferPointer();
  }
  else if (c == '/')
  {
    buffer[bufferPos] = '/';
    increaseBufferPointer();
  }
  else if (c == 'b')
  {
    buffer[bufferPos] = 0x08;
    increaseBufferPointer();
  }
  else if (c == 'f')
  {
    buffer[bufferPos] = '\f';
    increaseBufferPointer();
  }
  else if (c == 'n')
  {
    buffer[bufferPos] = '\n';
    increaseBufferPointer();
  }
  else if (c == 'r')
  {
    buffer[bufferPos] = '\r';
    increaseBufferPointer();
  }
  else if (c == 't')
  {
    buffer[bufferPos] = '\t';
    increaseBufferPointer();
  }
  else if (c == 'u')
  {
    state = STATE_UNICODE;
  }
  else
  {
    hasError = true;
    errorMessage = "Expected escaped character after backslash";
    return;
  }
  if (state != STATE_UNICODE)
  {
    state = STATE_IN_STRING;
  }
}

void JsonStreamingParser::processUnicodeCharacter(char c)
{
  if (!isHexCharacter(c))
  {
    hasError = true;
    errorMessage = "Expected hex character for escaped Unicode character";
    return;
  }

  unicodeBuffer[unicodeBufferPos] = c;
  unicodeBufferPos++;

  if (unicodeBufferPos == 4)
  {
    int codepoint = getHexArrayAsDecimal(unicodeBuffer, unicodeBufferPos);
    endUnicodeCharacter(codepoint);
    return;
    /*if (codepoint >= 0xD800 && codepoint < 0xDC00) {
      unicodeHighSurrogate = codepoint;
      unicodeBufferPos = 0;
      state = STATE_UNICODE_SURROGATE;
    } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
      if (unicodeHighSurrogate == -1) {
        // throw new ParsingError($this->_line_number,
        // $this->_char_number,
        // "Missing high surrogate for Unicode low surrogate.");
      }
      int combinedCodePoint = ((unicodeHighSurrogate - 0xD800) * 0x400) + (codepoint - 0xDC00) + 0x10000;
      endUnicodeCharacter(combinedCodePoint);
    } else if (unicodeHighSurrogate != -1) {
      // throw new ParsingError($this->_line_number,
      // $this->_char_number,
      // "Invalid low surrogate following Unicode high surrogate.");
      endUnicodeCharacter(codepoint);
    } else {
      endUnicodeCharacter(codepoint);
    }*/
  }
}
bool JsonStreamingParser::isHexCharacter(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int JsonStreamingParser::getHexArrayAsDecimal(char hexArray[], int length)
{
  int result = 0;
  for (int i = length - 1; i >= 0; i--)
  {
    char current = hexArray[length - i - 1];
    int value = 0;
    if (current >= 'a' && current <= 'f')
    {
      value = current - 'a' + 10;
    }
    else if (current >= 'A' && current <= 'F')
    {
      value = current - 'A' + 10;
    }
    else if (current >= '0' && current <= '9')
    {
      value = current - '0';
    }
    result = (result << 4) | value;
  }
  return result;
}

bool JsonStreamingParser::doesCharArrayContain(char myArray[], int length, char c)
{
  for (int i = 0; i < length; i++)
  {
    if (myArray[i] == c)
    {
      return true;
    }
  }
  return false;
}

void JsonStreamingParser::endUnicodeSurrogateInterstitial()
{
  char unicodeEscape = unicodeEscapeBuffer[unicodeEscapeBufferPos - 1];
  if (unicodeEscape != 'u')
  {
    // throw new ParsingError($this->_line_number, $this->_char_number,
    // "Expected '\\u' following a Unicode high surrogate. Got: " .
    // $unicode_escape);
  }
  unicodeBufferPos = 0;
  unicodeEscapeBufferPos = 0;
  state = STATE_UNICODE;
}

void JsonStreamingParser::endNumber()
{
  buffer[bufferPos] = '\0';
  if (strchr(buffer, '.') != NULL)
  {
    float floatValue;
    sscanf(buffer, "%f", &floatValue);
    cbFun({EVENT_VALUE, key, elementValue.with(floatValue)});
    key = {};
  }
  else
  {
    long intValue;
    sscanf(buffer, "%ld", &intValue);
    cbFun({EVENT_VALUE, key, elementValue.with(intValue)});
    key = {};
  }
  bufferPos = 0;
  state = STATE_AFTER_VALUE;
}

int JsonStreamingParser::convertDecimalBufferToInt(char myArray[], int length)
{
  int result = 0;
  for (int i = 0; i < length; i++)
  {
    char current = myArray[length - i - 1];
    result += (current - '0') * 10;
  }
  return result;
}

void JsonStreamingParser::endDocument()
{
  cbFun({EVENT_END_DOCUMENT, {}, {}});
  state = STATE_START_DOCUMENT;
  bufferPos = 0;
  unicodeEscapeBufferPos = 0;
  unicodeBufferPos = 0;
  characterCounter = 0;
}

void JsonStreamingParser::endTrue()
{
  buffer[bufferPos] = '\0';
  if (strcmp(buffer, "true") == 0)
  {
    cbFun({EVENT_VALUE, key, elementValue.with(true)});
    key = {};
  }
  else
  {
    hasError = true;
    errorMessage = "Expected 'true' literal";
    return;
  }
  bufferPos = 0;
  state = STATE_AFTER_VALUE;
}

void JsonStreamingParser::endFalse()
{
  buffer[bufferPos] = '\0';
  if (strcmp(buffer, "false") == 0)
  {
    cbFun({EVENT_VALUE, key, elementValue.with(false)});
    key = {};
  }
  else
  {
    hasError = true;
    errorMessage = "Expected 'false' literal";
    return;
  }
  bufferPos = 0;
  state = STATE_AFTER_VALUE;
}

void JsonStreamingParser::endNull()
{
  buffer[bufferPos] = '\0';
  if (strcmp(buffer, "null") == 0)
  {
    cbFun({EVENT_VALUE, key, elementValue.with()});
    key = {};
  }
  else
  {
    hasError = true;
    errorMessage = "Expected 'null' literal";
    return;
  }
  bufferPos = 0;
  state = STATE_AFTER_VALUE;
}

void JsonStreamingParser::startArray()
{
  cbFun({EVENT_START_ARRAY, key, {}});
  key = {};
  state = STATE_IN_ARRAY;
  stack[stackPos] = STACK_ARRAY;
  stackPos++;
}

void JsonStreamingParser::startObject()
{
  cbFun({EVENT_START_OBJECT, key, {}});
  key = {};
  state = STATE_IN_OBJECT;
  stack[stackPos] = STACK_OBJECT;
  stackPos++;
}

void JsonStreamingParser::startString(char c)
{
  stack[stackPos] = STACK_STRING;
  stackPos++;
  state = STATE_IN_STRING;
  stringDelimiter = c;
}

void JsonStreamingParser::startNumber(char c)
{
  state = STATE_IN_NUMBER;
  buffer[bufferPos] = c;
  increaseBufferPointer();
}

void JsonStreamingParser::endUnicodeCharacter(int codepoint)
{
  if (codepoint < 0x80)
  {
    buffer[bufferPos] = (char)(codepoint);
  }
  else if (codepoint < 0x800)
  {
    buffer[bufferPos] = (char)((codepoint >> 6) | 0b11000000);
    increaseBufferPointer();
    buffer[bufferPos] = (char)((codepoint & 0b00111111) | 0b10000000);
  }
  else if (codepoint == 0x2019)
  {
    buffer[bufferPos] = '\''; // \u2019 ’
  }
  else
  {
    buffer[bufferPos] = ' ';
  }
  increaseBufferPointer();
  unicodeBufferPos = 0;
  unicodeHighSurrogate = -1;
  state = STATE_IN_STRING;
}
