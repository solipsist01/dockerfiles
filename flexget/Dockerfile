FROM python:2-onbuild

VOLUME ["/flexget"]
VOLUME ["/input"]
VOLUME ["/output"]

WORKDIR /flexget

RUN git clone https://github.com/rg3/youtube-dl /youtube-dl && python /youtube-dl/setup.py install
    
CMD rm -f /flexget/.config-lock && flexget daemon start
