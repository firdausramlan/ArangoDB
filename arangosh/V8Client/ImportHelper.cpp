////////////////////////////////////////////////////////////////////////////////
/// @brief import helper
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
/// @author Achim Brandt
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2008-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "ImportHelper.h"

#include <sstream>
#include <iomanip>

#include "Basics/StringUtils.h"
#include "Basics/files.h"
#include "Basics/json.h"
#include "Basics/tri-strings.h"
#include "Rest/HttpRequest.h"
#include "SimpleHttpClient/SimpleHttpClient.h"
#include "SimpleHttpClient/SimpleHttpResult.h"

using namespace triagens::basics;
using namespace triagens::httpclient;
using namespace triagens::rest;
using namespace std;

////////////////////////////////////////////////////////////////////////////////
/// @brief helper function to determine if a field value is an integer
/// this function is here to avoid usage of regexes, which are too slow
////////////////////////////////////////////////////////////////////////////////

static bool IsInteger (char const* field, 
                       size_t fieldLength) {
  char const* end = field + fieldLength;

  if (*field == '+' || *field == '-') {
    ++field;
  }

  while (field < end) {
    if (*field < '0' || *field > '9') {
      return false;
    }
    ++field;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief helper function to determine if a field value maybe is a decimal
/// value. this function peeks into the first few bytes of the value only
/// this function is here to avoid usage of regexes, which are too slow
////////////////////////////////////////////////////////////////////////////////

static bool IsDecimal (char const* field,
                       size_t fieldLength) {
  char const* ptr = field;
  char const* end = ptr + fieldLength;

  if (*ptr == '+' || *ptr == '-') {
    ++ptr;
  }

  bool nextMustBeNumber = false;

  while (ptr < end) {
    if (*ptr == '.') {
      if (nextMustBeNumber) {
        return false;
      }
      // expect a number after the .
      nextMustBeNumber = true;
    }
    else if (*ptr == 'e' || *ptr == 'E') {
      if (nextMustBeNumber) {
        return false;
      }
      // expect a number after the exponent
      nextMustBeNumber = true;

      ++ptr;
      if (ptr >= end) {
        return false;
      }
      // skip over optional + or -
      if (*ptr == '+' || *ptr == '-') {
        ++ptr;
      }
      // do not advance ptr anymore
      continue;
    }
    else if (*ptr >= '0' && *ptr <= '9') {
      // found a number
      nextMustBeNumber = false;
    }
    else {
      // something else
      return false;
    }

    ++ptr;
  }

  if (nextMustBeNumber) {
    return false;
  }

  return true;
}


namespace triagens {
  namespace v8client {

////////////////////////////////////////////////////////////////////////////////
/// initialise step value for progress reports
////////////////////////////////////////////////////////////////////////////////

    const double ImportHelper::ProgressStep = 3.0;

////////////////////////////////////////////////////////////////////////////////
/// constructor and destructor
////////////////////////////////////////////////////////////////////////////////

    ImportHelper::ImportHelper (httpclient::SimpleHttpClient* client,
                                uint64_t maxUploadSize)
    : _client(client),
      _maxUploadSize(maxUploadSize),
      _separator(","),
      _quote("\""),
      _useBackslash(false),
      _createCollection(false),
      _overwrite(false),
      _progress(false),
      _firstChunk(true),
      _lineBuffer(TRI_UNKNOWN_MEM_ZONE),
      _outputBuffer(TRI_UNKNOWN_MEM_ZONE) {

      _hasError = false;
    }

    ImportHelper::~ImportHelper () {
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief imports a delmiited file
////////////////////////////////////////////////////////////////////////////////

    bool ImportHelper::importDelimited (string const& collectionName,
                                        string const& fileName,
                                        DelimitedImportType typeImport) {
      _collectionName = collectionName;
      _firstLine = "";
      _numberLines = 0;
      _numberOk = 0;
      _numberError = 0;
      _outputBuffer.clear();
      _lineBuffer.clear();
      _errorMessage = "";
      _hasError = false;

      // read and convert
      int fd;
      int64_t totalLength;

      if (fileName == "-") {
        // we don't have a filesize
        totalLength = 0;
        fd = STDIN_FILENO;
      }
      else {
        // read filesize
        totalLength = TRI_SizeFile(fileName.c_str());
        fd = TRI_OPEN(fileName.c_str(), O_RDONLY);

        if (fd < 0) {
          _errorMessage = TRI_LAST_ERROR_STR;
          return false;
        }
      }

      // progress display control variables
      int64_t totalRead = 0;
      double nextProgress = ProgressStep;

      size_t separatorLength;
      char* separator = TRI_UnescapeUtf8StringZ(TRI_UNKNOWN_MEM_ZONE, _separator.c_str(), _separator.size(), &separatorLength);

      if (separator == nullptr) {
        if (fd != STDIN_FILENO) {
          TRI_CLOSE(fd);
        }

        _errorMessage = "out of memory";
        return false;
      }

      TRI_csv_parser_t parser;

      TRI_InitCsvParser(&parser,
                        TRI_UNKNOWN_MEM_ZONE,
                        ProcessCsvBegin,
                        ProcessCsvAdd,
                        ProcessCsvEnd);

      TRI_SetSeparatorCsvParser(&parser, separator[0]);
      TRI_UseBackslashCsvParser(&parser, _useBackslash);

      // in csv, we'll use the quote char if set
      // in tsv, we do not use the quote char
      if (typeImport == ImportHelper::CSV && _quote.size() > 0) {
        TRI_SetQuoteCsvParser(&parser, _quote[0], true);
      }
      else {
        TRI_SetQuoteCsvParser(&parser, '\0', false);
      }
      parser._dataAdd = this;
      _rowOffset = 0;
      _rowsRead  = 0;

      char buffer[32768];

      while (! _hasError) {
        ssize_t n = TRI_READ(fd, buffer, sizeof(buffer));

        if (n < 0) {
          TRI_Free(TRI_UNKNOWN_MEM_ZONE, separator);
          TRI_DestroyCsvParser(&parser);
          if (fd != STDIN_FILENO) {
            TRI_CLOSE(fd);
          }
          _errorMessage = TRI_LAST_ERROR_STR;
          return false;
        }
        else if (n == 0) {
          break;
        }

        totalRead += (int64_t) n;
        reportProgress(totalLength, totalRead, nextProgress);

        TRI_ParseCsvString2(&parser, buffer, n);
      }

      if (_outputBuffer.length() > 0) {
        sendCsvBuffer();
      }

      TRI_DestroyCsvParser(&parser);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, separator);

      if (fd != STDIN_FILENO) {
        TRI_CLOSE(fd);
      }

      _outputBuffer.clear();
      return !_hasError;
    }

    bool ImportHelper::importJson (const string& collectionName, const string& fileName) {
      _collectionName = collectionName;
      _firstLine = "";
      _numberLines = 0;
      _numberOk = 0;
      _numberError = 0;
      _outputBuffer.clear();
      _errorMessage = "";
      _hasError = false;

      // read and convert
      int fd;
      int64_t totalLength;

      if (fileName == "-") {
        // we don't have a filesize
        totalLength = 0;
        fd = STDIN_FILENO;
      }
      else {
        // read filesize
        totalLength = TRI_SizeFile(fileName.c_str());
        fd = TRI_OPEN(fileName.c_str(), O_RDONLY);

        if (fd < 0) {
          _errorMessage = TRI_LAST_ERROR_STR;
          return false;
        }
      }

      bool isArray = false;
      bool checkedFront = false;

      // progress display control variables
      int64_t totalRead = 0;
      double nextProgress = ProgressStep;

      static const int BUFFER_SIZE = 32768;

      while (! _hasError) {
        // reserve enough room to read more data
        if (_outputBuffer.reserve(BUFFER_SIZE) == TRI_ERROR_OUT_OF_MEMORY) {
          _errorMessage = TRI_errno_string(TRI_ERROR_OUT_OF_MEMORY);

          if (fd != STDIN_FILENO) {
            TRI_CLOSE(fd);
          }
          return false;
        }

        // read directly into string buffer
        ssize_t n = TRI_READ(fd, _outputBuffer.end(), BUFFER_SIZE - 1);

        if (n < 0) {
          _errorMessage = TRI_LAST_ERROR_STR;
          if (fd != STDIN_FILENO) {
            TRI_CLOSE(fd);
          }
          return false;
        }
        else if (n == 0) {
          // we're done
          break;
        }

        // adjust size of the buffer by the size of the chunk we just read
        _outputBuffer.increaseLength(n);

        if (! checkedFront) {
          // detect the import file format (single lines with individual JSON objects
          // or a JSON array with all documents)
          char const* p = _outputBuffer.begin();
          char const* e = _outputBuffer.end();

          while (p < e &&
                 (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t' || *p == '\f' || *p == '\b')) {
            ++p;
          }

          isArray = (*p == '[');
          checkedFront = true;
        }

        totalRead += (int64_t) n;
        reportProgress(totalLength, totalRead, nextProgress);

        if (_outputBuffer.length() > _maxUploadSize) {
          if (isArray) {
            if (fd != STDIN_FILENO) {
              TRI_CLOSE(fd);
            }
            _errorMessage = "import file is too big. please increase the value of --batch-size (currently " + StringUtils::itoa(_maxUploadSize) + ")";
            return false;
          }

          // send all data before last '\n'
          char const* first = _outputBuffer.c_str();
          char* pos = (char*) memrchr(first, '\n', _outputBuffer.length());

          if (pos != nullptr) {
            size_t len = pos - first + 1;
            sendJsonBuffer(first, len, isArray);
            _outputBuffer.erase_front(len);
          }
        }
      }

      if (_outputBuffer.length() > 0) {
        sendJsonBuffer(_outputBuffer.c_str(), _outputBuffer.length(), isArray);
      }

      _numberLines = _numberError + _numberOk;

      if (fd != STDIN_FILENO) {
        TRI_CLOSE(fd);
      }

      _outputBuffer.clear();
      return ! _hasError;
    }

////////////////////////////////////////////////////////////////////////////////
/// private functions
////////////////////////////////////////////////////////////////////////////////

    void ImportHelper::reportProgress (int64_t totalLength,
                                       int64_t totalRead,
                                       double& nextProgress) {
      if (! _progress) {
        return;
      }

      if (totalLength == 0) {
        // length of input is unknown
        // in this case we cannot report the progress as a percentage
        // instead, report every 10 MB processed
        static int64_t nextProcessed = 10 * 1000 * 1000; 

        if (totalRead >= nextProcessed) {
          LOG_INFO("processed %lld bytes of input file", (long long) totalRead);
          nextProcessed += 10 * 1000 * 1000;
        }
      }
      else {
        double pct = 100.0 * ((double) totalRead / (double) totalLength);

        if (pct >= nextProgress && totalLength >= 1024) {
          LOG_INFO("processed %lld bytes (%0.1f%%) of input file", (long long) totalRead, nextProgress);
          nextProgress = (double) ((int) (pct + ProgressStep));
        }
      }
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief return the collection-related URL part
////////////////////////////////////////////////////////////////////////////////

    string ImportHelper::getCollectionUrlPart () {
      string part("collection=" + StringUtils::urlEncode(_collectionName));

      if (_firstChunk) {
        if (_createCollection) {
          part += "&createCollection=yes";
        }

        if (_overwrite) {
          part += "&overwrite=yes";
        }

        _firstChunk = false;
      }

      return part;
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief start a new csv line
////////////////////////////////////////////////////////////////////////////////

    void ImportHelper::ProcessCsvBegin (TRI_csv_parser_t* parser, size_t row) {
      static_cast<ImportHelper*>(parser->_dataAdd)->beginLine(row);
    }

    void ImportHelper::beginLine (size_t row) {
      if (_lineBuffer.length() > 0) {
        // error
        ++_numberError;
        _lineBuffer.clear();
      }

      ++_numberLines;

      if (row > 0) {
        _lineBuffer.appendChar('\n');
      }
      _lineBuffer.appendChar('[');
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a new CSV field
////////////////////////////////////////////////////////////////////////////////

    void ImportHelper::ProcessCsvAdd (TRI_csv_parser_t* parser, 
                                      char const* field, 
                                      size_t fieldLength, 
                                      size_t row, 
                                      size_t column, 
                                      bool escaped) {
      static_cast<ImportHelper*>(parser->_dataAdd)->addField(field, fieldLength, row, column, escaped);
    }

    void ImportHelper::addField (char const* field,
                                 size_t fieldLength, 
                                 size_t row, 
                                 size_t column, 
                                 bool escaped) {
      if (column > 0) {
        _lineBuffer.appendChar(',');
      }

      if (row == 0 || escaped) {
        // head line or escaped value
        _lineBuffer.appendChar('"');
        _lineBuffer.appendJsonEncoded(field);
        _lineBuffer.appendChar('"');
        return;
      }

      if (*field == '\0') {
        // do nothing
        _lineBuffer.appendText("null", strlen("null"));
        return;
      }

      // check for literals null, false and true
      if (fieldLength == 4 &&
          (memcmp(field, "true", 4) == 0 ||
           memcmp(field, "null", 4) == 0)) {
        _lineBuffer.appendText(field, fieldLength);
        return;
      }
      else if (fieldLength == 5 && memcmp(field, "false", 5) == 0) {
        _lineBuffer.appendText(field, fieldLength);
        return;
      }

      if (IsInteger(field, fieldLength)) {
        // integer value
        // conversion might fail with out-of-range error
        try {
          if (fieldLength > 8) {
            // long integer numbers might be problematic. check if we get out of range
            std::stoll(std::string(field, fieldLength)); // this will fail if the number cannot be converted
          }

          int64_t num = StringUtils::int64(field, fieldLength);
          _lineBuffer.appendInteger(num);
        }
        catch (...) {
          // conversion failed
          _lineBuffer.appendChar('"');
          _lineBuffer.appendJsonEncoded(field);
          _lineBuffer.appendChar('"');
        }
      }
      else if (IsDecimal(field, fieldLength)) {
        // double value
        // conversion might fail with out-of-range error
        try {
          double num = StringUtils::doubleDecimal(field, fieldLength);
          bool failed = (num != num || num == HUGE_VAL || num == -HUGE_VAL); 
          if (! failed) {
            _lineBuffer.appendDecimal(num);
            return;
          }
          // NaN, +inf, -inf
          // fall-through to appending the number as a string
        }
        catch (...) {
          // conversion failed
          // fall-through to appending the number as a string
        }

        _lineBuffer.appendChar('"');
        _lineBuffer.appendText(field, fieldLength);
        _lineBuffer.appendChar('"');
      }
      else {
        _lineBuffer.appendChar('"');
        _lineBuffer.appendJsonEncoded(field);
        _lineBuffer.appendChar('"');
      }
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief ends a CSV line
////////////////////////////////////////////////////////////////////////////////

    void ImportHelper::ProcessCsvEnd (TRI_csv_parser_t* parser, 
                                      char const* field, 
                                      size_t fieldLength,
                                      size_t row, 
                                      size_t column, 
                                      bool escaped) {
      ImportHelper* ih = static_cast<ImportHelper*>(parser->_dataAdd);

      if (ih) {
        ih->addLastField(field, fieldLength, row, column, escaped);
        ih->incRowsRead();
      }
    }

    void ImportHelper::addLastField (char const* field, 
                                     size_t fieldLength, 
                                     size_t row, 
                                     size_t column, 
                                     bool escaped) {
      if (column == 0 && *field == '\0') {
        // ignore empty line
        _lineBuffer.reset();
        return;
      }

      addField(field, fieldLength, row, column, escaped);

      _lineBuffer.appendChar(']');

      if (row == 0) {
        // save the first line
        _firstLine = _lineBuffer.c_str();
      }
      else if (row > 0 && _firstLine.empty()) {
        // error
        ++_numberError;
        _lineBuffer.reset();
        return;
      }

      // read a complete line

      if (_lineBuffer.length() > 0) {
        _outputBuffer.appendText(_lineBuffer);
        _lineBuffer.reset();
      }
      else {
        ++_numberError;
      }

      if (_outputBuffer.length() > _maxUploadSize) {
        sendCsvBuffer();
        _outputBuffer.appendText(_firstLine);
      }
    }


    void ImportHelper::sendCsvBuffer () {
      if (_hasError) {
        return;
      }

      map<string, string> headerFields;
      string url("/_api/import?" + getCollectionUrlPart() + "&line=" + StringUtils::itoa(_rowOffset) + "&details=true");
      SimpleHttpResult* result = _client->request(HttpRequest::HTTP_REQUEST_POST, url, _outputBuffer.c_str(), _outputBuffer.length(), headerFields);

      handleResult(result);

      _outputBuffer.reset();
      _rowOffset = _rowsRead;
    }

    void ImportHelper::sendJsonBuffer (char const* str, size_t len, bool isArray) {
      if (_hasError) {
        return;
      }

      map<string, string> headerFields;
      SimpleHttpResult* result;

      if (isArray) {
        result = _client->request(HttpRequest::HTTP_REQUEST_POST, "/_api/import?type=array&" + getCollectionUrlPart() + "&details=true", str, len, headerFields);
      }
      else {
        result = _client->request(HttpRequest::HTTP_REQUEST_POST, "/_api/import?type=documents&" + getCollectionUrlPart() + "&details=true", str, len, headerFields);
      }

      handleResult(result);
    }

    void ImportHelper::handleResult (SimpleHttpResult* result) {
      if (result == nullptr) {
        return;
      }

      TRI_json_t* json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE,
                                        result->getBody().c_str());

      if (json != nullptr) {
        // error details
        TRI_json_t const* details = TRI_LookupArrayJson(json, "details");

        if (TRI_IsListJson(details)) {
          size_t const n = details->_value._objects._length;

          for (size_t i = 0; i < n; ++i) {
            TRI_json_t const* detail = static_cast<TRI_json_t const*>(TRI_AtVector(&details->_value._objects, i));

            if (TRI_IsStringJson(detail)) {
              LOG_WARNING("%s", detail->_value._string.data);
            }
          }
        }

        // get the "error" flag. This returns a pointer, not a copy
        TRI_json_t const* error = TRI_LookupArrayJson(json, "error");

        if (TRI_IsBooleanJson(error) &&
            error->_value._boolean) {
          _hasError = true;

          // get the error message. This returns a pointer, not a copy
          TRI_json_t const* errorMessage = TRI_LookupArrayJson(json, "errorMessage");

          if (TRI_IsStringJson(errorMessage)) {
            _errorMessage = string(errorMessage->_value._string.data, errorMessage->_value._string.length - 1);
          }
        }

        TRI_json_t const* importResult;

        // look up the "created" flag. This returns a pointer, not a copy
        importResult = TRI_LookupArrayJson(json, "created");

        if (TRI_IsNumberJson(importResult)) {
          _numberOk += (size_t) importResult->_value._number;
        }

        // look up the "errors" flag. This returns a pointer, not a copy
        importResult = TRI_LookupArrayJson(json, "errors");

        if (TRI_IsNumberJson(importResult)) {
          _numberError += (size_t) importResult->_value._number;
        }

        // this will free the json struct will a sub-elements
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }

      delete result;
    }

  }
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
