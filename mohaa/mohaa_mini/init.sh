#!/bin/bash

if [ ! -f /config/server.cfg ]; then
   cp /app/main/server.cfg_orig /config/server.cfg
fi

ln -s /config/server.cfg /app/main/server.cfg

/app/mohaa_lnxded +set dedicated 1 +exec server.cfg