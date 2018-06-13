FROM python:2-onbuild

VOLUME ["/flexget"]
VOLUME ["/input"]
VOLUME ["/output"]

WORKDIR /flexget

RUN git clone https://github.com/rg3/youtube-dl /youtube-dl && cd /youtube-dl && python setup.py install
    
CMD rm -f /flexget/.config-lock && flexget --loglevel trace daemon start
