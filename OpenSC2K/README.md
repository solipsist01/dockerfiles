# OpenSC2K Docker container

You can run this container like this:

```
docker run \
--name=opensc2k \
-d \
-p 3000:3000 \
solipsist01/opensc2k
```

# OpenSC2K
OpenSC2K - An Open Source remake of SimCity 2000 written in JavaScript, using WebGL Canvas and [Phaser 3](https://github.com/photonstorm/phaser/).

## Overview
Currently a lot remains to be implemented but the basic framework is there for importing and viewing cities. Lots of stuff remains completely unimplemented such as the actual simulation, rendering of many special case tiles and buildings and anything else that exists outside of importing and viewing.

Along with implementing the original functionality and features, I plan to add additional capabilities beyond the original such as larger city/map sizes, additional network types, adding buildings beyond the initial tileset limitations, action/history tracking along with replays and more.

I've only tested using Chrome 64 on macOS, but it should run fairly well on any modern browser/platform that supports WebGL. Performance should be acceptable but there is still a LOT of room for optimizations and improvements.

![Screenshot](https://raw.githubusercontent.com/solipsist01/dockerfiles/master/OpenSC2K/screenshots/1.png)