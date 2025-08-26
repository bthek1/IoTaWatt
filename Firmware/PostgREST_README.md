# PostgREST Uploader Configuration

> PostgREST Uploader

The PostgREST uploader allows you to send IoTaWatt data to a PostgreSQL database through a PostgREST API endpoint.

## Configuration Example

Add this configuration to your IoTaWatt config.txt file:

```json
{
  "postgrest": [{
    "type": "postgrest",
    "url": "https://your-postgrest-server.com",
    "table": "power_measurements",
    "timeColumn": "timestamp",
    "valueColumn": "value",
    "nameColumn": "measurement_name",
    "schema": "public",
    "apiKey": "your-api-key",
    "jwtToken": "your-jwt-token",
    "batchSize": 100,
    "postInterval": 60,
    "bulksend": 5,
    "outputs": [
      {
        "name": "main_power",
        "script": "input_0",
        "units": "Watts"
      },
      {
        "name": "solar_power", 
        "script": "input_1",
        "units": "Watts"
      }
    ]
  }]
}
```

## Configuration Parameters

### Required Parameters

- **url**: The base URL of your PostgREST server
- **table**: The database table name where data will be inserted
- **outputs**: Array of measurement outputs to send

### Optional Parameters

- **timeColumn**: Database column name for timestamps (default: "timestamp")
- **valueColumn**: Database column name for measurement values (default: "value")  
- **nameColumn**: Database column name for measurement names (default: "name")
- **schema**: Database schema name (default: "public")
- **batchSize**: Number of records to send per request (default: 100, max: 1000)
- **postInterval**: Seconds between uploads (default: 60, must be multiple of 5)
- **bulksend**: Number of intervals to batch together (default: 1)

### Authentication Parameters (choose one)

- **apiKey**: API key for authentication
- **jwtToken**: JWT token for authentication

## Database Schema

Your PostgreSQL table should have a structure like:

```sql
CREATE TABLE power_measurements (
    id SERIAL PRIMARY KEY,
    timestamp INTEGER NOT NULL,  -- Unix timestamp
    measurement_name VARCHAR(50) NOT NULL,
    value REAL NOT NULL
);

-- Add indexes for better performance
CREATE INDEX idx_power_timestamp ON power_measurements(timestamp);
CREATE INDEX idx_power_name ON power_measurements(measurement_name);
```

## PostgREST Configuration

Ensure your PostgREST configuration allows:

1. INSERT operations on your target table
2. Proper authentication (API key or JWT)
3. CORS if accessing from web interface

Example postgrest.conf:

```yaml
db-uri = "postgres://username:password@localhost/database"
db-schema = "public"
db-anon-role = "web_anon"
```

## Monitoring

Check the uploader status via the IoTaWatt web interface:

- Go to `/status?postgrest=yes` to see PostgREST uploader status
- Monitor logs for upload success/failure messages

## Troubleshooting

Common issues:

1. **Authentication errors**: Verify API key or JWT token
2. **Schema errors**: Ensure table structure matches expected columns
3. **Permission errors**: Check PostgREST role permissions for INSERT operations
4. **Network errors**: Verify URL accessibility and SSL certificates
