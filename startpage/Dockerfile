FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    STARTPAGE_DB=/data/startpage.db \
    FLASK_APP=startpage

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY startpage.py ./
COPY templates ./templates
COPY static ./static

RUN useradd --system --uid 10001 --no-create-home startpage \
    && mkdir /data && chown startpage:startpage /data
USER startpage

VOLUME /data
EXPOSE 8000
CMD ["gunicorn", "--bind", "0.0.0.0:8000", "--workers", "2", "--preload", "--access-logfile", "-", "startpage:app"]
