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
        log("%s: Config - table: %s", _id, _table);
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
    log("%s: Config - timeColumn: %s", _id, _timeColumn);
    
    if (Json.containsKey("valueColumn")) {
        delete[] _valueColumn;
        _valueColumn = charstar(Json["valueColumn"].as<char*>());
    } else {
        delete[] _valueColumn;
        _valueColumn = charstar("value");
    }
    log("%s: Config - valueColumn: %s", _id, _valueColumn);
    
    if (Json.containsKey("nameColumn")) {
        delete[] _nameColumn;
        _nameColumn = charstar(Json["nameColumn"].as<char*>());
    } else {
        delete[] _nameColumn;
        _nameColumn = charstar("name");
    }
    log("%s: Config - nameColumn: %s", _id, _nameColumn);
    
    // Parse schema (optional, default to public)
    if (Json.containsKey("schema")) {
        delete[] _schema;
        _schema = charstar(Json["schema"].as<char*>());
    } else {
        delete[] _schema;
        _schema = charstar("public");
    }
    log("%s: Config - schema: %s", _id, _schema);
    
    // Parse authentication
    if (Json.containsKey("apiKey")) {
        delete[] _apiKey;
        _apiKey = charstar(Json["apiKey"].as<char*>());
        log("%s: Config - apiKey set: %s", _id, _apiKey ? "yes" : "no");
    }
    
    if (Json.containsKey("jwtToken")) {
        delete[] _jwtToken;
        _jwtToken = charstar(Json["jwtToken"].as<char*>());
        log("%s: Config - jwtToken set: %s", _id, _jwtToken ? "yes" : "no");
    }
    
    // Parse batch size
    if (Json.containsKey("batchSize")) {
        _batchSize = Json["batchSize"].as<int>();
        if (_batchSize < 1 || _batchSize > 1000) {
            _batchSize = 100;
        }
    }
    log("%s: Config - batchSize: %d", _id, _batchSize);
    
    trace(T_postgrest,111);
    return true;
}

/*****************************************************************************************
 * Parse PostgreSQL timestamp string to UNIX timestamp
 * Format: "YYYY-MM-DD HH:mm:ss+00:00"
 * ***************************************************************************************/
uint32_t postgrest_uploader::parseTimestamp(const char* timestampStr) {
    int year, month, day, hour, minute, second;
    if (sscanf(timestampStr, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
        return Unixtime(year, month, day, hour, minute, second);
    }
    return 0;
}

/*****************************************************************************************
 * Query last record from PostgREST to determine resume point
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_query_s() {
    trace(T_postgrest,120);
    
    // Build query URL for GET request
    String endpoint = "/";
    if (_schema && strcmp(_schema, "public") != 0) {
        endpoint += _schema;
        endpoint += ".";
    }
    endpoint += _table;
    endpoint += "?select=timestamp&device=eq.";
    endpoint += deviceName;
    endpoint += "&order=timestamp.desc&limit=1";
    
    log("%s: Query URL: %s", _id, endpoint.c_str());
    log("%s: Device name: %s", _id, deviceName);
    log("%s: Schema: %s", _id, _schema ? _schema : "null");
    log("%s: Table: %s", _id, _table ? _table : "null");
    
    // Use custom HTTPGet method for proper GET request
    HTTPGet(endpoint.c_str(), checkQuery_s);
    return 1;
}

/*****************************************************************************************
 * Check query response to determine last sent timestamp
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_checkQuery_s() {
    trace(T_postgrest,130);
    
    // Handle query response from _request (GET request)
    log("%s: Query response HTTP code: %d", _id, _request->responseHTTPcode());
    log("%s: Query response body: %s", _id, _request->responseText().c_str());
    
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
        if (lastRecord.containsKey("timestamp")) {
            // Convert PostgreSQL timestamp string to UNIX timestamp
            String timestampStr = lastRecord["timestamp"].as<String>();
            // Parse ISO format timestamp: "YYYY-MM-DD hh:mm:ss+00:00"
            // For now, we'll extract just the date/time part and convert to UNIX
            // This is a simplified parsing - production might need more robust parsing
            uint32_t timestamp = parseTimestamp(timestampStr.c_str());
            if (timestamp > 0) {
                _lastSent = timestamp;
                if (_lastSent >= MAX(Current_log.firstKey(), _uploadStartDate)) {
                    log("%s: Resume posting from %s", _id, localDateString(_lastSent + _interval).c_str());
                    _state = write_s;
                    return 1;
                }
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
                
                // Build JSON object for PostgREST table (fixed schema like C++ simulator)
                reqData.print("{");
                reqData.printf("\"timestamp\":\"%s\",", datef(oldRecord->UNIXtime, "YYYY-MM-DD hh:mm:ss").c_str());
                reqData.printf("\"device\":\"%s\",", deviceName);
                reqData.printf("\"sensor\":\"%s\",", script->name());
                reqData.printf("\"power\":%.*f,", script->precision(), value);
                reqData.print("\"pf\":null,\"current\":null,\"v\":null");
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
    
    log("%s: Write URL: %s", _id, endpoint.c_str());
    log("%s: Write JSON payload size: %d bytes", _id, reqData.available());
    // Log first part of JSON payload without consuming the buffer
    String preview = reqData.peekString(500);
    log("%s: Write JSON payload preview: %s", _id, preview.c_str());
    
    HTTPPost(endpoint.c_str(), checkWrite_s, "application/json");
    return 1;
}

/*****************************************************************************************
 * Check write response
 * ***************************************************************************************/
uint32_t postgrest_uploader::handle_checkWrite_s() {
    trace(T_postgrest,150);
    
    log("%s: Write response HTTP code: %d", _id, _request->responseHTTPcode());
    log("%s: Write response body: %s", _id, _request->responseText().c_str());
    
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

/*****************************************************************************************
 * Custom HTTPGet method for PostgREST queries following framework pattern
 * ***************************************************************************************/
void postgrest_uploader::HTTPGet(const char* endpoint, states completionState) {
    // Build a GET request control block for this request,
    // set state to checkQuery_s and return to caller.
    // Actual GET is done in next tick handler.
    
    if (!_GETrequest) {
        _GETrequest = new GETrequest;
    }
    delete _GETrequest->endpoint;
    _GETrequest->endpoint = charstar(endpoint);
    _GETrequest->completionState = completionState;
    
    // Check WiFi connectivity
    if (!WiFi.isConnected()) {
        log("%s: HTTPGet: not connected", _id);
        return;
    }
    
    // Setup request object like base uploader does
    if (!_request) {
        _request = new asyncHTTPrequest;
    }
    
    log("%s: HTTPGet: endpoint=%s", _id, endpoint);
    
    // Set timeout
    _request->setTimeout(10);
    _request->setDebug(false);
    
    // Build full URL using char buffer like base uploader
    char URL[300];  // Larger buffer for GET query parameters
    size_t len = snprintf(URL, 300, "%s%s", _url->build().c_str(), endpoint);
    
    if (!_request->open("GET", URL)) {
        log("%s: HTTPGet: open failed", _id);
        delete _request;
        _request = nullptr;
        return;
    }
    
    // Add headers 
    if (_jwtToken) {
        String auth = "Bearer ";
        auth += _jwtToken;
        _request->setReqHeader("Authorization", auth.c_str());
    }
    if (_apiKey) {
        _request->setReqHeader("apikey", _apiKey);
    }
    _request->setReqHeader("Accept", "application/json");
    _request->setReqHeader("User-Agent", "IoTaWatt");
    
    // Send the GET request (no body)
    if (!_request->send()) {
        log("%s: HTTPGet: send failed", _id);
        delete _request;
        _request = nullptr;
    } else {
        log("%s: HTTPGet: request sent successfully", _id);
        _state = completionState;  // Set state to completion handler
    }
}
