FROM python:3.9-slim

WORKDIR /app

# Install dependencies
RUN pip install --no-cache-dir influxdb-client requests

# Copy bridge script
COPY particle-bridge.py .

# Run the bridge
CMD ["python", "-u", "particle-bridge.py"]
