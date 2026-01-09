# Use lightweight Python image
FROM python:3.12-alpine

# Set working directory
WORKDIR /app

# Copy app + calculator HTML + requirements
COPY journal.py position_size_calculator.html requirements.txt /app/

# Install dependencies
RUN pip install --no-cache-dir -r requirements.txt \
    && mkdir -p /app/data

# Expose Flask port
EXPOSE 80

# Run the app
CMD ["python", "journal.py"]
