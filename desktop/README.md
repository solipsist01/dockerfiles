# desktop

Desktop enviornment in a docker container.

run like this:

```
docker run \
--name=desktop \
-d \
-p 6880:80 \
-e VNC_PASSWORD='Y0URUB3RSTR0NGP4SSW0RDG03SH3Re' \
solipsist01/desktop
```

![Screenshot](https://raw.githubusercontent.com/solipsist01/dockerfiles/master/desktop/screenshots/desktop.png)