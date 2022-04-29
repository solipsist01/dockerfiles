FROM wiserain/flexget

RUN apk update && apk add git && git clone https://github.com/rg3/youtube-dl /youtube-dl && cd /youtube-dl && python setup.py install && pip install flexget --upgrade

    

