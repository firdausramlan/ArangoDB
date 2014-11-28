////////////////////////////////////////////////////////////////////////////////
/// @brief json objects
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "json.h"
#include "Basics/files.h"
#include "Basics/logging.h"
#include "Basics/string-buffer.h"
#include "Basics/tri-strings.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                              JSON
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief prints a json object
////////////////////////////////////////////////////////////////////////////////

static int StringifyJson (TRI_memory_zone_t* zone,
                          TRI_string_buffer_t* buffer,
                          TRI_json_t const* object,
                          bool braces) {
  int res;

  switch (object->_type) {
    case TRI_JSON_UNUSED: {
      break;
    }

    case TRI_JSON_NULL: {
      res = TRI_AppendString2StringBuffer(buffer, "null", 4); // strlen("null")

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }

      break;
    }

    case TRI_JSON_BOOLEAN: {
      if (object->_value._boolean) {
        res = TRI_AppendString2StringBuffer(buffer, "true", 4); // strlen("true")
      }
      else {
        res = TRI_AppendString2StringBuffer(buffer, "false", 5); // strlen("false")
      }

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }

      break;
    }

    case TRI_JSON_NUMBER: {
      res = TRI_AppendDoubleStringBuffer(buffer, object->_value._number);

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }

      break;
    }

    case TRI_JSON_STRING:
    case TRI_JSON_STRING_REFERENCE: {
      res = TRI_AppendCharStringBuffer(buffer, '\"');

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }

      if (object->_value._string.length > 0) {
        // optimisation for the empty string
        res =  TRI_AppendJsonEncodedStringStringBuffer(buffer, object->_value._string.data, false);

        if (res != TRI_ERROR_NO_ERROR) {
          return TRI_ERROR_OUT_OF_MEMORY;
        }
      }

      res = TRI_AppendCharStringBuffer(buffer, '\"');

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }

      break;
    }

    case TRI_JSON_ARRAY: {
      size_t i, n;

      if (braces) {
        res = TRI_AppendCharStringBuffer(buffer, '{');

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      n = object->_value._objects._length;

      for (i = 0;  i < n;  i += 2) {
        if (0 < i) {
          res = TRI_AppendCharStringBuffer(buffer, ',');

          if (res != TRI_ERROR_NO_ERROR) {
            return res;
          }
        }

        res = StringifyJson(zone, buffer, (const TRI_json_t*) TRI_AtVector(&object->_value._objects, i), true);

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }

        res = TRI_AppendCharStringBuffer(buffer, ':');

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }

        res = StringifyJson(zone, buffer, (const TRI_json_t*) TRI_AtVector(&object->_value._objects, i + 1), true);

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      if (braces) {
        res = TRI_AppendCharStringBuffer(buffer, '}');

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      break;
    }

    case TRI_JSON_LIST: {
      size_t i, n;

      if (braces) {
        res = TRI_AppendCharStringBuffer(buffer, '[');

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      n = object->_value._objects._length;

      for (i = 0;  i < n;  ++i) {
        if (0 < i) {
          res = TRI_AppendCharStringBuffer(buffer, ',');

          if (res != TRI_ERROR_NO_ERROR) {
            return res;
          }
        }

        res = StringifyJson(zone, buffer, (const TRI_json_t*) TRI_AtVector(&object->_value._objects, i), true);

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      if (braces) {
        res = TRI_AppendCharStringBuffer(buffer, ']');

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      break;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a null object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitNull (TRI_json_t* result) {
  result->_type = TRI_JSON_NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a boolean object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitBoolean (TRI_json_t* result,
                                bool value) {
  result->_type = TRI_JSON_BOOLEAN;
  result->_value._boolean = value;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a number object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitNumber (TRI_json_t* result,
                               double value) {
  result->_type = TRI_JSON_NUMBER;
  result->_value._number = value;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a string object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitString (TRI_json_t* result,
                               char* value,
                               size_t length) {
  result->_type = TRI_JSON_STRING;
  result->_value._string.data = value;
  result->_value._string.length = (uint32_t) length + 1;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a string reference object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitStringReference (TRI_json_t* result,
                                        const char* value,
                                        size_t length) {
  result->_type = TRI_JSON_STRING_REFERENCE;
  result->_value._string.data = (char*) value;
  result->_value._string.length = (uint32_t) length + 1;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a list object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitList (TRI_memory_zone_t* zone,
                             TRI_json_t* result,
                             size_t initialSize) {
  result->_type = TRI_JSON_LIST;
  if (initialSize == 0) {
    TRI_InitVector(&result->_value._objects, zone, sizeof(TRI_json_t));
  }
  else {
    TRI_InitVector2(&result->_value._objects, zone, sizeof(TRI_json_t), initialSize);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise an array object in place
////////////////////////////////////////////////////////////////////////////////

static inline void InitArray (TRI_memory_zone_t* zone,
                              TRI_json_t* result,
                              size_t initialSize) {
  result->_type = TRI_JSON_ARRAY;

  if (initialSize == 0) {
    TRI_InitVector(&result->_value._objects, zone, sizeof(TRI_json_t));
  }
  else {
    // need to allocate twice the space because for each array entry,
    // we need one object for the attribute key, and one for the attribute value
    TRI_InitVector2(&result->_value._objects, zone, sizeof(TRI_json_t), 2 * initialSize);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determine whether a JSON value is of type string
////////////////////////////////////////////////////////////////////////////////

static inline bool IsString (TRI_json_t const* json) {
  return (json != NULL &&
          (json->_type == TRI_JSON_STRING || json->_type == TRI_JSON_STRING_REFERENCE) &&
          json->_value._string.data != NULL);
}

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a null object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateNullJson (TRI_memory_zone_t* zone) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitNull(result);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a null object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitNullJson (TRI_json_t* result) {
  InitNull(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a boolean object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateBooleanJson (TRI_memory_zone_t* zone, bool value) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitBoolean(result, value);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a boolean object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitBooleanJson (TRI_json_t* result, bool value) {
  InitBoolean(result, value);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a number object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateNumberJson (TRI_memory_zone_t* zone, double value) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitNumber(result, value);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a number object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitNumberJson (TRI_json_t* result, double value) {
  InitNumber(result, value);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a string object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateStringJson (TRI_memory_zone_t* zone, char* value) {
  return TRI_CreateString2Json(zone, value, strlen(value));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a string object with given length
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateString2Json (TRI_memory_zone_t* zone, char* value, size_t length) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitString(result, value, length);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a string object, copying the string
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateStringCopyJson (TRI_memory_zone_t* zone, char const* value) {
  return TRI_CreateString2CopyJson(zone, value, strlen(value));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a string object with given length, copying the string
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateString2CopyJson (TRI_memory_zone_t* zone, char const* value, size_t length) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitString(result, TRI_DuplicateString2Z(zone, value, length), length);

  if (result->_value._string.data == NULL) {
    TRI_Free(zone, result);
    return NULL;
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a string object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitStringJson (TRI_json_t* result, char* value) {
  InitString(result, value, strlen(value));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a string object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitString2Json (TRI_json_t* result, char* value, size_t length) {
  InitString(result, value, length);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a string object
////////////////////////////////////////////////////////////////////////////////

int TRI_InitStringCopyJson (TRI_memory_zone_t* zone, TRI_json_t* result, char const* value) {
  return TRI_InitString2CopyJson(zone, result, value, strlen(value));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a string object
////////////////////////////////////////////////////////////////////////////////

int TRI_InitString2CopyJson (TRI_memory_zone_t* zone, TRI_json_t* result, char const* value, size_t length) {
  char* copy = TRI_DuplicateString2Z(zone, value, length);
  if (copy == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  InitString(result, copy, length);
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a string reference object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateStringReferenceJson (TRI_memory_zone_t* zone, const char* value) {
  return TRI_CreateStringReference2Json(zone, value, strlen(value));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a string reference object with given length
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateStringReference2Json (TRI_memory_zone_t* zone, const char* value, size_t length) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitStringReference(result, value, length);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a string reference object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitStringReferenceJson (TRI_json_t* result, const char* value) {
  InitStringReference(result, value, strlen(value));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a string reference object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitStringReference2Json (TRI_json_t* result, const char* value, size_t length) {
  InitStringReference(result, value, length);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a list object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateListJson (TRI_memory_zone_t* zone) {
  return TRI_CreateList2Json(zone, 0);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a list object, with the specified initial size
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateList2Json (TRI_memory_zone_t* zone,
                                 const size_t initialSize) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitList(zone, result, initialSize);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a list object
////////////////////////////////////////////////////////////////////////////////

void TRI_InitListJson (TRI_memory_zone_t* zone, TRI_json_t* result) {
  InitList(zone, result, 0);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a list object with a given size
////////////////////////////////////////////////////////////////////////////////

void TRI_InitList2Json (TRI_memory_zone_t* zone, TRI_json_t* result, size_t length) {
  InitList(zone, result, length);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateArrayJson (TRI_memory_zone_t* zone) {
  return TRI_CreateArray2Json(zone, 0);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CreateArray2Json (TRI_memory_zone_t* zone,
                                  const size_t initialSize) {
  TRI_json_t* result;

  result = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (result == NULL) {
    return NULL;
  }

  InitArray(zone, result, initialSize);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises an array
////////////////////////////////////////////////////////////////////////////////

void TRI_InitArrayJson (TRI_memory_zone_t* zone, TRI_json_t* result) {
  InitArray(zone, result, 0);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises an array, using a specific initial size
////////////////////////////////////////////////////////////////////////////////

void TRI_InitArray2Json (TRI_memory_zone_t* zone,
                         TRI_json_t* result,
                         size_t initialSize) {
  InitArray(zone, result, initialSize);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a json object, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyJson (TRI_memory_zone_t* zone, TRI_json_t* object) {
  switch (object->_type) {
    case TRI_JSON_UNUSED:
    case TRI_JSON_NULL:
    case TRI_JSON_BOOLEAN:
    case TRI_JSON_NUMBER:
      break;

    case TRI_JSON_STRING:
      TRI_DestroyBlob(zone, &object->_value._string);
      break;

    case TRI_JSON_STRING_REFERENCE:
      // we will not be destroying the string!!
      break;

    case TRI_JSON_ARRAY:
    case TRI_JSON_LIST: {
      size_t i, n;
      n = object->_value._objects._length;

      for (i = 0;  i < n;  ++i) {
        TRI_json_t* v = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i);
        TRI_DestroyJson(zone, v);
      }

      TRI_DestroyVector(&object->_value._objects);
      break;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a json object and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeJson (TRI_memory_zone_t* zone, TRI_json_t* object) {
  TRI_DestroyJson(zone, object);
  TRI_Free(zone, object);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a user printeable string
////////////////////////////////////////////////////////////////////////////////

const char *TRI_GetTypeString(TRI_json_t* object) {
  switch (object->_type) {
    case TRI_JSON_UNUSED:
      return "unused";
    case TRI_JSON_NULL:
      return "null";
    case TRI_JSON_BOOLEAN:
      return "boolean";
    case TRI_JSON_NUMBER:
      return "number";
    case TRI_JSON_STRING:
      return "string";
    case TRI_JSON_STRING_REFERENCE:
      return "string-reference";
    case TRI_JSON_ARRAY:
      return "array";
    case TRI_JSON_LIST:
      return "list";
  }
  return "unknown";
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determines the length of a list json
////////////////////////////////////////////////////////////////////////////////

size_t TRI_LengthListJson (TRI_json_t const* json) {
  TRI_ASSERT(json != NULL && json->_type==TRI_JSON_LIST);
  return json->_value._objects._length;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determines whether the JSON passed is of type array
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsArrayJson (TRI_json_t const* json) {
  return json != NULL && json->_type == TRI_JSON_ARRAY;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determines whether the JSON passed is of type list
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsListJson (TRI_json_t const* json) {
  return json != NULL && json->_type == TRI_JSON_LIST;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determines whether the JSON passed is of type string
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsStringJson (TRI_json_t const* json) {
  return IsString(json);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determines whether the JSON passed is of type number
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsNumberJson (TRI_json_t const* json) {
  return json != NULL && json->_type == TRI_JSON_NUMBER;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determines whether the JSON passed is of type boolean
////////////////////////////////////////////////////////////////////////////////

bool TRI_IsBooleanJson (TRI_json_t const* json) {
  return json != NULL && json->_type == TRI_JSON_BOOLEAN;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new sub-object to a list object, copying it
////////////////////////////////////////////////////////////////////////////////

void TRI_PushBackListJson (TRI_memory_zone_t* zone, TRI_json_t* list, TRI_json_t const* object) {
  TRI_json_t copy;

  TRI_ASSERT(list->_type == TRI_JSON_LIST);

  TRI_CopyToJson(zone, &copy, object);

  TRI_PushBackVector(&list->_value._objects, &copy);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new sub-object to a list object, not copying it
////////////////////////////////////////////////////////////////////////////////

int TRI_PushBack2ListJson (TRI_json_t* list, TRI_json_t const* object) {
  TRI_ASSERT(list->_type == TRI_JSON_LIST);
  TRI_ASSERT(object);

  return TRI_PushBackVector(&list->_value._objects, object);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new sub-object, not copying it but freeing the pointer
////////////////////////////////////////////////////////////////////////////////

int TRI_PushBack3ListJson (TRI_memory_zone_t* zone, TRI_json_t* list, TRI_json_t* object) {
  int res;

  if (object == NULL) {
    return TRI_ERROR_INTERNAL;
  }

  res = TRI_PushBack2ListJson(list, object);
  TRI_Free(zone, object);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a value in a json list
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_LookupListJson (TRI_json_t const* object, size_t pos) {
  size_t n;

  TRI_ASSERT(object->_type == TRI_JSON_LIST);

  n = object->_value._objects._length;

  if (pos >= n) {
    // out of bounds
    return NULL;
  }

  return (TRI_json_t*) TRI_AtVector(&object->_value._objects, pos);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new attribute to an object, using copy
////////////////////////////////////////////////////////////////////////////////

void TRI_InsertArrayJson (TRI_memory_zone_t* zone,
                          TRI_json_t* object,
                          char const* name,
                          TRI_json_t const* subobject) {
  TRI_json_t copy;
  char* att;
  size_t length;

  TRI_ASSERT(object->_type == TRI_JSON_ARRAY);

  if (subobject == NULL) {
    return;
  }

  if (TRI_ReserveVector(&object->_value._objects, 2) != TRI_ERROR_NO_ERROR) {
    // TODO: signal OOM here
    return;
  }

  // attribute name
  length = strlen(name);
  att = TRI_DuplicateString2Z(zone, name, length);

  if (att == NULL) {
    // TODO: signal OOM here
    return;
  }

  InitString(&copy, att, length);
  TRI_PushBackVector(&object->_value._objects, &copy);

  // attribute value
  TRI_CopyToJson(zone, &copy, subobject);
  TRI_PushBackVector(&object->_value._objects, &copy);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new attribute to an object, not copying it
////////////////////////////////////////////////////////////////////////////////

void TRI_Insert2ArrayJson (TRI_memory_zone_t* zone,
                           TRI_json_t* object,
                           char const* name,
                           TRI_json_t* subobject) {
  TRI_json_t copy;
  char* att;
  size_t length;

  TRI_ASSERT(object->_type == TRI_JSON_ARRAY);

  if (subobject == NULL) {
    return;
  }

  if (TRI_ReserveVector(&object->_value._objects, 2) != TRI_ERROR_NO_ERROR) {
    // TODO: signal OOM here
    return;
  }

  length = strlen(name);
  att = TRI_DuplicateString2Z(zone, name, length);

  if (att == NULL) {
    // TODO: signal OOM here
    return;
  }

  // attribute name
  InitString(&copy, att, length);
  TRI_PushBackVector(&object->_value._objects, &copy);

  // attribute value
  TRI_PushBackVector(&object->_value._objects, subobject);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new attribute, not copying it but freeing the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_Insert3ArrayJson (TRI_memory_zone_t* zone, TRI_json_t* object, char const* name, TRI_json_t* subobject) {
  if (object != NULL && subobject != NULL) {
    TRI_Insert2ArrayJson(zone, object, name, subobject);
    TRI_Free(zone, subobject);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new attribute name and value
////////////////////////////////////////////////////////////////////////////////

void TRI_Insert4ArrayJson (TRI_memory_zone_t* zone, TRI_json_t* object, char* name, size_t nameLength, TRI_json_t* subobject, bool asReference) {
  TRI_json_t copy;

  TRI_ASSERT(name != NULL);

  // attribute name
  if (asReference) {
    InitStringReference(&copy, name, nameLength);
  }
  else {
    InitString(&copy, name, nameLength);
  }

  if (TRI_ReserveVector(&object->_value._objects, 2) != TRI_ERROR_NO_ERROR) {
    // TODO: signal OOM here
    return;
  }

  TRI_PushBackVector(&object->_value._objects, &copy);

  // attribute value
  TRI_PushBackVector(&object->_value._objects, subobject);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an attribute in an json array
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_LookupArrayJson (const TRI_json_t* const object, char const* name) {
  size_t i, n;

  if (object == NULL) {
    return NULL;
  }

  TRI_ASSERT(object->_type == TRI_JSON_ARRAY);
  TRI_ASSERT(name != NULL);

  n = object->_value._objects._length;

  for (i = 0;  i < n;  i += 2) {
    TRI_json_t* key;

    key = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i);

    if (! IsString(key)) {
      continue;
    }

    if (TRI_EqualString(key->_value._string.data, name)) {
      return (TRI_json_t*) TRI_AtVector(&object->_value._objects, i + 1);
    }
  }

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes an element from a json array
////////////////////////////////////////////////////////////////////////////////

bool TRI_DeleteArrayJson (TRI_memory_zone_t* zone, TRI_json_t* object, char const* name) {
  size_t n;
  size_t i;

  TRI_ASSERT(object->_type == TRI_JSON_ARRAY);
  TRI_ASSERT(name);

  n = object->_value._objects._length;

  for (i = 0;  i < n;  i += 2) {
    TRI_json_t* key;

    key = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i);

    if (! IsString(key)) {
      continue;
    }

    if (TRI_EqualString(key->_value._string.data, name)) {
      TRI_json_t* old;

      // remove key
      old = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i);
      if (old != NULL) {
        TRI_DestroyJson(zone, old);
      }
      TRI_RemoveVector(&object->_value._objects, i);

      // remove value
      old = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i);
      if (old != NULL) {
        TRI_DestroyJson(zone, old);
      }
      TRI_RemoveVector(&object->_value._objects, i);

      return true;
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replaces an element in a json array
////////////////////////////////////////////////////////////////////////////////

bool TRI_ReplaceArrayJson (TRI_memory_zone_t* zone, TRI_json_t* object, char const* name, TRI_json_t* replacement) {
  size_t n;
  size_t i;

  TRI_ASSERT(object->_type == TRI_JSON_ARRAY);
  TRI_ASSERT(name);

  n = object->_value._objects._length;

  for (i = 0;  i < n;  i += 2) {
    TRI_json_t* key;

    key = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i);

    if (! IsString(key)) {
      continue;
    }

    if (TRI_EqualString(key->_value._string.data, name)) {
      TRI_json_t copy;

      // retrieve the old element
      TRI_json_t* old = (TRI_json_t*) TRI_AtVector(&object->_value._objects, i + 1);
      if (old != NULL) {
        TRI_DestroyJson(zone, old);
      }

      TRI_CopyToJson(zone, &copy, replacement);
      TRI_SetVector(&object->_value._objects, i + 1, &copy);
      return true;
    }
  }

  // object not found in array, now simply add it
  TRI_Insert2ArrayJson(zone, object, name, replacement);

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringifies a json object
////////////////////////////////////////////////////////////////////////////////

int TRI_StringifyJson (TRI_string_buffer_t* buffer, TRI_json_t const* object) {
  return StringifyJson(buffer->_memoryZone, buffer, object, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringifies a json object skipping the outer braces
////////////////////////////////////////////////////////////////////////////////

int TRI_Stringify2Json (TRI_string_buffer_t* buffer, TRI_json_t const* object) {
  return StringifyJson(buffer->_memoryZone, buffer, object, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief prints a json object
////////////////////////////////////////////////////////////////////////////////

bool TRI_PrintJson (int fd, 
                    TRI_json_t const* object,
                    bool appendNewline) {
  TRI_string_buffer_t buffer;
  char const* p;
  size_t n;

  if (object == NULL) {
    // sanity check
    return false;
  }

  TRI_InitStringBuffer(&buffer, TRI_UNKNOWN_MEM_ZONE);
  if (StringifyJson(buffer._memoryZone, &buffer, object, true) != TRI_ERROR_NO_ERROR) {
    TRI_AnnihilateStringBuffer(&buffer);
    return false;
  }

  if (TRI_LengthStringBuffer(&buffer) == 0) {
    // should not happen
    return false;
  }

  if (appendNewline) {
    // add the newline here so we only need one write operation in the ideal case
    TRI_AppendCharStringBuffer(&buffer, '\n');
  }

  p = TRI_BeginStringBuffer(&buffer);
  n = TRI_LengthStringBuffer(&buffer);

  while (0 < n) {
    ssize_t m = TRI_WRITE(fd, p, (TRI_write_t) n);

    if (m <= 0) {
      TRI_AnnihilateStringBuffer(&buffer);
      return false;
    }

    n -= m;
    p += m;
  }

  TRI_AnnihilateStringBuffer(&buffer);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves a json object
////////////////////////////////////////////////////////////////////////////////

bool TRI_SaveJson (char const* filename,
                   TRI_json_t const* object,
                   bool syncFile) {
  char* tmp;
  int fd;
  int res;

  tmp = TRI_Concatenate2String(filename, ".tmp");

  if (tmp == NULL) {
    return false;
  }

  // remove a potentially existing temporary file
  if (TRI_ExistsFile(tmp)) {
    TRI_UnlinkFile(tmp);
  }

  fd = TRI_CREATE(tmp, O_CREAT | O_TRUNC | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);

  if (fd < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    LOG_ERROR("cannot create json file '%s': %s", tmp, TRI_LAST_ERROR_STR);
    TRI_FreeString(TRI_CORE_MEM_ZONE, tmp);
    return false;
  }

  if (! TRI_PrintJson(fd, object, true)) {
    TRI_CLOSE(fd);
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    LOG_ERROR("cannot write to json file '%s': %s", tmp, TRI_LAST_ERROR_STR);
    TRI_UnlinkFile(tmp);
    TRI_FreeString(TRI_CORE_MEM_ZONE, tmp);
    return false;
  }
  
  if (syncFile) {
    LOG_TRACE("syncing tmp file '%s'", tmp);

    if (! TRI_fsync(fd)) {
      TRI_CLOSE(fd);
      TRI_set_errno(TRI_ERROR_SYS_ERROR);
      LOG_ERROR("cannot sync saved json '%s': %s", tmp, TRI_LAST_ERROR_STR);
      TRI_UnlinkFile(tmp);
      TRI_FreeString(TRI_CORE_MEM_ZONE, tmp);
      return false;
    }
  }

  res = TRI_CLOSE(fd);

  if (res < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    LOG_ERROR("cannot close saved file '%s': %s", tmp, TRI_LAST_ERROR_STR);
    TRI_UnlinkFile(tmp);
    TRI_FreeString(TRI_CORE_MEM_ZONE, tmp);
    return false;
  }

  res = TRI_RenameFile(tmp, filename);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(res);
    LOG_ERROR("cannot rename saved file '%s' to '%s': %s", tmp, filename, TRI_LAST_ERROR_STR);
    TRI_UnlinkFile(tmp);
    TRI_FreeString(TRI_CORE_MEM_ZONE, tmp);

    return false;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, tmp);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief copies a json object into a given buffer
////////////////////////////////////////////////////////////////////////////////

int TRI_CopyToJson (TRI_memory_zone_t* zone,
                    TRI_json_t* dst,
                    TRI_json_t const* src) {
  int res;
  size_t n;
  size_t i;

  dst->_type = src->_type;

  switch (src->_type) {
    case TRI_JSON_UNUSED:
    case TRI_JSON_NULL:
      break;

    case TRI_JSON_BOOLEAN:
      dst->_value._boolean = src->_value._boolean;
      break;

    case TRI_JSON_NUMBER:
      dst->_value._number = src->_value._number;
      break;

    case TRI_JSON_STRING:
      return TRI_CopyToBlob(zone, &dst->_value._string, &src->_value._string);

    case TRI_JSON_STRING_REFERENCE:
      return TRI_AssignToBlob(zone, &dst->_value._string, &src->_value._string);

    case TRI_JSON_ARRAY:
    case TRI_JSON_LIST:
      n = src->_value._objects._length;

      TRI_InitVector(&dst->_value._objects, zone, sizeof(TRI_json_t));
      res = TRI_ResizeVector(&dst->_value._objects, n);

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }

      for (i = 0;  i < n;  ++i) {
        TRI_json_t const* v = static_cast<TRI_json_t const*>(TRI_AtVector(&src->_value._objects, i));
        TRI_json_t* w = static_cast<TRI_json_t*>(TRI_AtVector(&dst->_value._objects, i));

        res = TRI_CopyToJson(zone, w, v);

        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }

      break;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief copies a json object
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_CopyJson (TRI_memory_zone_t* zone, 
                          TRI_json_t const* src) {
  TRI_json_t* dst;
  int res;

  dst = (TRI_json_t*) TRI_Allocate(zone, sizeof(TRI_json_t), false);

  if (dst == NULL) {
    return NULL;
  }

  res = TRI_CopyToJson(zone, dst, src);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(zone, dst);
    return NULL;
  }

  return dst;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json object into a number
////////////////////////////////////////////////////////////////////////////////

int64_t TRI_ToInt64Json (TRI_json_t* json) {
  switch (json->_type) {
    case TRI_JSON_UNUSED:
    case TRI_JSON_NULL:
      return 0;
    case TRI_JSON_BOOLEAN:
      return (json->_value._boolean ? 1 : 0);
    case TRI_JSON_NUMBER:
      return static_cast<int64_t>(json->_value._number);
    case TRI_JSON_STRING:
    case TRI_JSON_STRING_REFERENCE:
      try {
        // try converting string to number
        double v = std::stod(json->_value._string.data);
        return static_cast<int64_t>(v);
      }
      catch (...) {
        // conversion failed
      }
      break;
    case TRI_JSON_LIST: {
      size_t const n = TRI_LengthListJson(json);
      if (n == 0) {
        return 0;
      }
      else if (n == 1) {
        return TRI_ToInt64Json(TRI_LookupListJson(json, 0));
      }
      break;
    }
    case TRI_JSON_ARRAY:
      break;
  }
  
  // TODO: must convert to null here
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json object into a number
////////////////////////////////////////////////////////////////////////////////

double TRI_ToDoubleJson (TRI_json_t* json) {
  switch (json->_type) {
    case TRI_JSON_UNUSED:
    case TRI_JSON_NULL:
      return 0.0;
    case TRI_JSON_BOOLEAN:
      return (json->_value._boolean ? 1.0 : 0.0);
    case TRI_JSON_NUMBER:
      return json->_value._number;
    case TRI_JSON_STRING:
    case TRI_JSON_STRING_REFERENCE:
      try {
        // try converting string to number
        double v = std::stod(json->_value._string.data);
        return v;
      }
      catch (...) {
        // conversion failed
      }
      break;
    case TRI_JSON_LIST: {
      size_t const n = TRI_LengthListJson(json);
      if (n == 0) {
        return 0.0;
      }
      else if (n == 1) {
        return TRI_ToDoubleJson(TRI_LookupListJson(json, 0));
      }
      break;
    }
    case TRI_JSON_ARRAY:
      break;
  }

  // TODO: must convert to null here
  return 0.0;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
