#include "postgrest_uploader.h"
#include "splitstr.h"

/*****************************************************************************************
 * PostgREST uploader implementation
 * 
 * This uploader sends data to a PostgREST API endpoint, which provides a RESTful 
 * interface to PostgreSQL databases.
 * 
 * Configuration JSON format:
 * {
 *   "type": "postgrest",
 *   "url": "https://your-postgrest-server.com",
 *   "table": "power_data",
 *   "timeColumn": "timestamp",
 *   "valueColumn": "value", 
 *   "nameColumn": "name",
 *   "schema": "public",
 *   "apiKey": "your-api-key",
 *   "jwtToken": "your-jwt-token",
 *   "batchSize": 100,
 *   "postInterval": 60,
 *   "outputs": [...]
 * }
 * ***************************************************************************************/

uint32_t postgrest_dispatch(struct serviceBlock *serviceBlock) {
    trace(T_postgrest,0);
    postgrest_uploader *_this = (postgrest_uploader *)serviceBlock->serviceParm;
    trace(T_postgrest,1);
    uint32_t reschedule = _this->dispatch(serviceBlock);
    trace(T_postgrest,1);
    if(reschedule){
        return reschedule;
    } 
    trace(T_postgrest,0);
    return 0;
}

/*****************************************************************************************
 * PostgREST uploader dispatch method
 * ***************************************************************************************/
uint32_t postgrest_uploader::dispatch(struct serviceBlock *serviceBlock) {
    return uploader::dispatch(serviceBlock);
}

/*****************************************************************************************
 * Configuration callback - parses JSON config
 * ***************************************************************************************/
bool postgrest_uploader::configCB(const char *JsonText) {
    DynamicJsonBuffer JsonBuffer;
    JsonObject& Json = JsonBuffer.parseObject(JsonText);
    if (!Json.success()) {
        log("%s: JSON parse failed", _id);
        return false;
    }
    return configCB(Json);
}

bool postgrest_uploader::configCB(JsonObject &Json) {
    trace(T_postgrest,110);
    
    // Parse table name (required)
    if (Json.containsKey("table")) {
        delete[] _table;
        _table = charstar(Json["table"].as<char*>());
    } else {
        log("%s: table name required", _id);
        return false;
    }
    
    // Parse column names (with defaults)
    if (Json.containsKey("timeColumn")) {
        delete[] _timeColumn;
        _timeColumn = charstar(Json["timeColumn"].as<char*>());
    } else {
        delete[] _timeColumn;
        _timeColumn = charstar("timestamp");
    }
    
    if (Json.containsKey("valueColumn")) {
        delete[] _valueColumn;
        _valueColumn = charstar(Json["valueColumn"].as<char*>());
    } else {
        delete[] _valueColumn;
        _valueColumn = charstar("value");
    }
    
    if (Json.containsKey("nameColumn")) {
        delete[] _nameColumn;
        _nameColumn = charstar(Json["nameColumn"].as<char*>());
    } else {
        delete[] _nameColumn;
        _nameColumn = charstar("name");
    }
    
    // Parse schema (optional, default to public)
    if (Json.containsKey("schema")) {
        delete[] _schema;
        _schema = charstar(Json["schema"].as<char*>());
    } else {
        delete[] _schema;
        _schema = charstar("public");
    }
    
    // Parse authentication
    if (Json.containsKey("apiKey")) {
        delete[] _apiKey;
        _apiKey = charstar(Json["apiKey"].as<char*>());
    }
    
    if (Json.containsKey("jwtToken")) {
        delete[] _jwtToken;
        _jwtToken = charstar(Json["jwtToken"].as<char*>());
    }
    
    // Parse batch size
    if (Json.containsKey("batchSize")) {
        _batchSize = Json["batchSize"].as<int>();
        if (_batchSize < 1 || _batchSize > 1000) {
            _batchSize = 100;
        }
    }
    
    trace(T_postgrest,111);
    return true;
}

/*****************************************************************************************
 * Query last record from PostgREST to determine resume point
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_query_s() {
    trace(T_postgrest,120);
    
    // Build query to get the last timestamp for resuming uploads
    reqData.flush();
    
    // PostgREST query format: GET /table?select=timeColumn&order=timeColumn.desc&limit=1
    String endpoint = "/";
    if (_schema && strcmp(_schema, "public") != 0) {
        endpoint += _schema;
        endpoint += ".";
    }
    endpoint += _table;
    endpoint += "?select=";
    endpoint += _timeColumn;
    endpoint += "&order=";
    endpoint += _timeColumn;
    endpoint += ".desc&limit=1";
    
    HTTPPost(endpoint.c_str(), checkQuery_s, "application/json");
    return 1;
}

/*****************************************************************************************
 * Check query response to determine last sent timestamp
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_checkQuery_s() {
    trace(T_postgrest,130);
    
    // Handle query response
    if (_request->responseHTTPcode() != 200) {
        delete[] _statusMessage;
        _statusMessage = charstar(F("Query failed. HTTPcode "), String(_request->responseHTTPcode()).c_str());
        log("%s: %s", _id, _statusMessage);
        delay(60, query_s);
        return 1;
    }
    
    // Parse JSON response
    String response = _request->responseText();
    DynamicJsonBuffer JsonBuffer;
    JsonArray& jsonArray = JsonBuffer.parseArray(response);
    
    if (jsonArray.success() && jsonArray.size() > 0) {
        JsonObject& lastRecord = jsonArray[0];
        if (lastRecord.containsKey(_timeColumn)) {
            _lastSent = lastRecord[_timeColumn].as<uint32_t>();
            if (_lastSent >= MAX(Current_log.firstKey(), _uploadStartDate)) {
                log("%s: Resume posting from %s", _id, localDateString(_lastSent + _interval).c_str());
                _state = write_s;
                return 1;
            }
        }
    }
    
    // No valid last record found, start from beginning
    _lastSent = MAX(Current_log.firstKey(), _uploadStartDate);
    if (_uploadStartDate) {
        _lastSent = _uploadStartDate;
    }
    _lastSent -= _lastSent % _interval;
    log("%s: Start posting from %s", _id, localDateString(_lastSent + _interval).c_str());
    _state = write_s;
    return 1;
}

/*****************************************************************************************
 * Build and send data to PostgREST endpoint
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_write_s() {
    trace(T_postgrest,140);
    
    if (_stop) {
        stop();
        return 1;
    }
    
    // Check if we have enough data to post
    if (Current_log.lastKey() < (_lastSent + _interval + (_interval * _bulkSend))) {
        if (oldRecord) {
            delete oldRecord;
            oldRecord = nullptr;
            delete newRecord;
            newRecord = nullptr;
        }
        return UTCtime() + 1;
    }
    
    // Allocate datalog buffers if needed
    if (!oldRecord) {
        oldRecord = new IotaLogRecord;
        newRecord = new IotaLogRecord;
        newRecord->UNIXtime = _lastSent + _interval;
        Current_log.readKey(newRecord);
    }
    
    // Build JSON array for batch insert
    reqData.flush();
    reqData.print("[");
    
    bool firstRecord = true;
    int recordCount = 0;
    
    // Process multiple records up to batch size
    while (recordCount < _batchSize && 
           reqData.available() < uploaderBufferLimit && 
           newRecord->UNIXtime < Current_log.lastKey()) {
        
        if (micros() > bingoTime) {
            return 10;
        }
        
        // Swap records and read next
        IotaLogRecord *swap = oldRecord;
        oldRecord = newRecord;
        newRecord = swap;
        newRecord->UNIXtime = oldRecord->UNIXtime + _interval;
        Current_log.readKey(newRecord);
        
        // Calculate elapsed time
        double elapsedHours = newRecord->logHours - oldRecord->logHours;
        if (elapsedHours == 0) {
            if ((newRecord->UNIXtime + _interval) <= Current_log.lastKey()) {
                return 1;
            }
            return UTCtime() + 1;
        }
        
        // Process each output script
        Script *script = _outputs->first();
        while (script) {
            double value = script->run(oldRecord, newRecord);
            if (value == value) { // Check for NaN
                if (!firstRecord) {
                    reqData.print(",");
                }
                
                // Build JSON object for this measurement
                reqData.print("{");
                reqData.printf("\"%s\":%u,", _timeColumn, oldRecord->UNIXtime);
                reqData.printf("\"%s\":\"%s\",", _nameColumn, script->name());
                reqData.printf("\"%s\":%.*f", _valueColumn, script->precision(), value);
                reqData.print("}");
                
                firstRecord = false;
                recordCount++;
            }
            script = script->next();
        }
        
        _lastPost = oldRecord->UNIXtime;
    }
    
    reqData.print("]");
    
    // Clean up records
    delete oldRecord;
    oldRecord = nullptr;
    delete newRecord;
    newRecord = nullptr;
    
    // Send the batch
    String endpoint = "/";
    if (_schema && strcmp(_schema, "public") != 0) {
        endpoint += _schema;
        endpoint += ".";
    }
    endpoint += _table;
    
    HTTPPost(endpoint.c_str(), checkWrite_s, "application/json");
    return 1;
}

/*****************************************************************************************
 * Check write response
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_checkWrite_s() {
    trace(T_postgrest,150);
    
    // PostgREST returns 201 for successful inserts
    if (_request->responseHTTPcode() == 201) {
        delete[] _statusMessage;
        _statusMessage = nullptr;
        _lastSent = _lastPost;
        _state = write_s;
        return 1;
    }
    
    // Handle errors
    delete[] _statusMessage;
    _statusMessage = charstar(F("Write failed. HTTPcode "), String(_request->responseHTTPcode()).c_str());
    log("%s: %s", _id, _statusMessage);
    log("%s: Response: %s", _id, _request->responseText().c_str());
    
    // Retry with delay
    delay(60, write_s);
    return 1;
}

/*****************************************************************************************
 * Set authentication headers
 * ***************************************************************************************/
void postgrest_uploader::setRequestHeaders() {
    _request->setReqHeader("Content-Type", "application/json");
    _request->setReqHeader("Accept", "application/json");
    _request->setReqHeader("Prefer", "return=minimal");
    
    if (_apiKey) {
        _request->setReqHeader("apikey", _apiKey);
    }
    
    if (_jwtToken) {
        String auth = "Bearer ";
        auth += _jwtToken;
        _request->setReqHeader("Authorization", auth.c_str());
    }
}

/*****************************************************************************************
 * Script comparison for sorting
 * ***************************************************************************************/
int postgrest_uploader::scriptCompare(Script *a, Script *b) {
    return strcmp(a->name(), b->name());
}
