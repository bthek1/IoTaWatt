#ifndef postgrest_uploader_h
#define postgrest_uploader_h

#include "IotaWatt.h"

extern uint32_t postgrest_dispatch(struct serviceBlock *serviceBlock);

class postgrest_uploader : public uploader 
{
    public:
        postgrest_uploader() :
            _table(0),
            _timeColumn(0),
            _valueColumn(0),
            _nameColumn(0),
            _apiKey(0),
            _jwtToken(0),
            _schema(0),
            _batchSize(100)
        {
            _id = charstar("postgrest");
        };

        ~postgrest_uploader(){
            delete[] _table;
            delete[] _timeColumn;
            delete[] _valueColumn;
            delete[] _nameColumn;
            delete[] _apiKey;
            delete[] _jwtToken;
            delete[] _schema;
            postgrest = nullptr;
        };

        bool configCB(const char *JsonText);
        uint32_t dispatch(struct serviceBlock *serviceBlock);

    protected:
        char *_table;
        char *_timeColumn;
        char *_valueColumn;
        char *_nameColumn;
        char *_apiKey;
        char *_jwtToken;
        char *_schema;
        uint16_t _batchSize;

        uint32_t handle_query_s();
        uint32_t handle_checkQuery_s();
        uint32_t handle_write_s();
        uint32_t handle_checkWrite_s();
        bool configCB(JsonObject &);

        void setRequestHeaders();
        int scriptCompare(Script *a, Script *b);
};

#endif
