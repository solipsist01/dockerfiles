FROM mhart/alpine-node:base

ENV PATH /OpenSC2K:/root/.yarn/bin:$PATH

RUN apk update \
  && apk add curl bash binutils tar git \
  && rm -rf /var/cache/apk/* \
  && /bin/bash \
  && touch ~/.bashrc \
  && curl -o- -L https://yarnpkg.com/install.sh | bash \
  && git clone https://github.com/rage8885/OpenSC2K \
  && cd OpenSC2K \
  && yarn --ignore-engines install \
  && apk del git curl tar binutils
  
EXPOSE 3000

ENTRYPOINT [ "/bin/bash" ]

CMD [ "yarn run start" ]