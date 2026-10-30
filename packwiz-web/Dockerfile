FROM golang:1.25-alpine AS packwiz
RUN apk add --no-cache git \
    && CGO_ENABLED=0 go install github.com/packwiz/packwiz@latest \
    && wget -q -O /packwiz-installer-bootstrap.jar \
       https://github.com/packwiz/packwiz-installer-bootstrap/releases/latest/download/packwiz-installer-bootstrap.jar

FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PACKWIZ_DATA=/data

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY --from=packwiz /go/bin/packwiz /usr/local/bin/packwiz
COPY --from=packwiz /packwiz-installer-bootstrap.jar /app/packwiz-installer-bootstrap.jar
COPY packwiz_web.py resourcepack_migrate.py ./
COPY templates ./templates
COPY static ./static

RUN useradd --system --uid 10001 --no-create-home packwiz \
    && mkdir /data && chown packwiz:packwiz /data
USER packwiz

VOLUME /data
EXPOSE 8000
CMD ["gunicorn", "--bind", "0.0.0.0:8000", "--workers", "2", "--threads", "4", "--timeout", "900", "--preload", "--no-control-socket", "--access-logfile", "-", "packwiz_web:app"]
