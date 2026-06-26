#ifndef ELEMENT_VALUE_H
#define ELEMENT_VALUE_H

#include <cstring>

union Variant {
  bool boolValue;
  float numValue;
  const char* stringValue;
};

struct ElementValue {
  private:
    static const int Type_None = 30;
    static const int Type_Null = 31;
    static const int Type_Int = 32;
    static const int Type_Float = 33;
    static const int Type_String = 34;
    static const int Type_Bool = 35;

    Variant data;
    int type;

  public:
    ElementValue with(float value) {
      data.numValue = value;
      type = Type_Float;
      return *this;
    }

    ElementValue with(long value) {
      data.numValue = value;
      type = Type_Int;
      return *this;
    }
    
    ElementValue with(bool value) {
      data.boolValue = value;
      type = Type_Bool;
      return *this;
    }

    ElementValue with(const char* value) {
      data.stringValue = value;
      type = Type_String;
      return *this;
    }

    ElementValue with() {
      type = Type_Null;
      return *this;
    }
    
    bool getBool() {
      return data.boolValue;
    }

    const std::string getString() {
      return std::string(data.stringValue);
    }

    float getFloat() {
      return data.numValue;
    }

    long getInt() {
      return (long)data.numValue;
    }

    bool isInt() {
      return type == Type_Int;
    }

    bool isFloat() {
      return type == Type_Float;
    }

    bool isString() {
      return type == Type_String;
    }

    bool isBool() {
      return type == Type_Bool;
    }

    bool isNull() {
      return type == Type_Null;
    }
};

#endif // ELEMENT_VALUE_H