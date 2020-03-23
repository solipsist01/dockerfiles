# desktop

Desktop environment in a docker container.

Contains:
```
MKVToolnix
Handbrake
GemistDownloader
Gnome Commander
```


# Docker Run

```
docker run \
--name=desktop \
-d \
-p 6880:80 \
-e VNC_PASSWORD='Y0URUB3RSTR0NGP4SSW0RDG03SH3Re' \
solipsist01/desktop
```

# Screenshot

![Screenshot](https://raw.githubusercontent.com/solipsist01/dockerfiles/master/desktop/screenshots/desktop.png)