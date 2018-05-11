# phpvirtualbox 5.2 Docker image

This is a fork of [jazzdd/phpvirtualbox](https://hub.docker.com/r/jazzdd/phpvirtualbox/) which is a fork of [clue/phpvirtualbox](https://hub.docker.com/r/clue/phpvirtualbox/). This is an update to the latest PHP virtualbox version for virtualbox 5.2


## phpVirtualBox 5.2

[phpVirtualBox](http://sourceforge.net/projects/phpvirtualbox/) is a modern web interface that allows you to control remote VirtualBox instances - mirroring the VirtualBox GUI.

![](http://a.fsdn.com/con/app/proj/phpvirtualbox/screenshots/phpvb1.png)

## Usage
This image provides the phpVirtualBox web interface that communicates with any number of VirtualBox installations on your computers.

Internally, the phpVirtualBox web interface communicates with each VirtualBox installation through the `vboxwebsrv` program that is installed as part of VirtualBox.

The container is started with following command:

# Install Virtualbox on your host machine
```
add-apt-repository \
   "http://download.virtualbox.org/virtualbox/debian \
   $(lsb_release -cs) \
   contrib"
wget https://www.virtualbox.org/download/oracle_vbox_2016.asc -O- | apt-key add -
apt update
apt install virtualbox-5.2
```
# Create vbox user on host:
```
sudo useradd -d /home/vbox -m -g vboxusers -s /bin/bash vbox
```

# if user alerady exists, and you don't know the password
```
passwd vbox
```

# Create vboxwebservice on host:
```
echo 'VBOXWEB_USER=vbox' | tee -a /etc/default/virtualbox
echo 'VBOXWEB_HOST=127.0.0.1' | tee -a /etc/default/virtualbox
systemctl enable vboxweb-service
systemctl start vboxweb-service
```
# Run the docker container:
```
docker run --name vbox_http --restart=always \
    -p 80:80 \
    -e ID_PORT_18083_TCP=ServerIP:PORT \
    -e ID_NAME=serverName \
    -e ID_USER=vboxUser \
    -e ID_PW='vboxUserPassword' \
    -e CONF_browserRestrictFolders="/data,/home" \
    -d solipsist01/phpvirtualbox
```

# parameter breakdown

* `-p {OutsidePort}:80` - will bind the webserver to the given host port
* `-d solipsist01/phpvirtualbox` - the name of this docker image
* `-e ID_NAME` - name of the vbox server
* `-e ID_PORT_18083_TCP` - ip/hostname and port of the vbox server
* `-e ID_USER` - user name of the user in the vbox group
* `-e ID_PW` - password of this user
* `-e CONF_varName` - override default config value of varName, browserRestrictFolders is a useful example. Coma-separated strings will be converted into an array.

ID is an identifier to get all matching environment variables for one vbox server. So, it is possible to define more then one VirtualBox server and manage it with one phpVirtualbox instance.

An example would look as follows:
```
docker run --name vbox_http --restart=always -p 80:80 \
    -e SRV1_PORT_18083_TCP=192.168.1.1:18083 -e SRV1_NAME=Server1 -e SRV1_USER=user1 -e SRV1_PW='test' \
    -e SRV2_PORT_18083_TCP=192.168.1.2:18083 -e SRV2_NAME=Server2 -e SRV2_USER=user2 -e SRV2_PW='test' \
    -d solipsist01/phpvirtualbox
```

