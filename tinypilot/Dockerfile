FROM python:3

RUN apt-get update && apt-get -y install shellcheck

RUN curl -sL https://deb.nodesource.com/setup_14.x |  bash -
RUN  apt-get install -y nodejs

RUN git clone https://github.com/mtlynch/tinypilot

WORKDIR /tinypilot

RUN pip install --requirement requirements.txt && pip install --requirement dev_requirements.txt
RUN npm install prettier@2.0.5

RUN ./dev-scripts/build

EXPOSE 8000
ENV PORT=8000


CMD ["python", "/tinypilot/app/main.py"]