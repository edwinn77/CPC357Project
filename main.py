import functions_framework
from google.cloud import pubsub_v1
from google.cloud import bigquery
from datetime import datetime, timedelta
import json

# Initialize Clients
bq_client = bigquery.Client()
publisher = pubsub_v1.PublisherClient()
PROJECT_ID = "cpc357-481703"        #project id
TOPIC_NAME = "bus-occupancy-topic"  #topic name
topic_path = publisher.topic_path(PROJECT_ID, TOPIC_NAME)

def get_authorized_limit(route_name):
    """Fetches capacity based on the specific route sent by sensor."""
    try:
        job_config = bigquery.QueryJobConfig(use_query_cache=False)
        query = f"""
            SELECT TotalBuses 
            FROM `cpc357-481703.bus_data_dataset.busnumber` 
            WHERE Route = '{route_name}' 
            LIMIT 1
        """
        query_job = bq_client.query(query, job_config=job_config)
        results = query_job.result()
        
        for row in results:
            return int(row.TotalBuses) * 10 
            
    except Exception as e:
        print(f"DB Error: {str(e)}")
    return 101 # Fallback

@functions_framework.http
def bus_logger(request):
    request_json = request.get_json(silent=True)
    
    if not request_json or 'passenger_count' not in request_json:
        return ({"status": "fail"}, 400)

    # 1. Extract dynamic data from Sensor
    count = int(request_json['passenger_count'])
    event = request_json.get('event', 'Sensor Update')
    route_name = request_json.get('route', 'Route A')
    current_stop = request_json.get('location', 'In Transit')

    # 2. Process logic
    max_allowed = get_authorized_limit(route_name)
    percentage = (count / max(1, max_allowed)) * 100

    # 3. Handle Time
    if 'manual_timestamp' in request_json:
        timestamp_str = request_json['manual_timestamp']
    else:
        malaysia_time = datetime.utcnow() + timedelta(hours=8)
        timestamp_str = malaysia_time.strftime('%Y-%m-%d %H:%M:%S')

    # 4. Final Payload for BigQuery
    payload = {
        "timestamp": timestamp_str,
        "passenger_count": count,
        "event_type": event,
        "authorized_capacity": max_allowed,
        "percentage_capacity": round(percentage, 2),
        "route": route_name,
        "location": current_stop # Added location field
    }
    
    try:
        publisher.publish(topic_path, json.dumps(payload).encode("utf-8"))
        return ({"status": "success", "route": route_name, "stop": current_stop}, 200)
    except Exception as e:
        return ({"status": "error", "message": str(e)}, 500)