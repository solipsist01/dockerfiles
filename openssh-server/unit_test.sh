#!/bin/bash

function set_return() { return ${1:-0}; }

countdown() {
  secs=$1
  shift
  msg=$@
  while [ $secs -gt 0 ]
  do
    printf "\r\033[KWaiting %.d seconds $msg" $((secs--))
    sleep 1
  done
  echo
}

echo "genereren keypair voor de test"
ssh-keygen -b 2048 -t rsa -f openssh-server/key -q -N ""
echo "lezen van public key in variabele voor starten container"
publickey=$(cat openssh-server/key.pub)
echo "Starten van openssh_test docker container voor test"
docker run -d --name=openssh_test --network=nasi -e PUID=1000 -e PGID=1000 -e TZ=europe/amsterdam -e PUBLIC_KEY="$publickey" -e SUDO_ACCESS=false -e PASSWORD_ACCESS=false -e USER_NAME=test -l traefik.enable=false solipsist01/openssh-server
echo "wachten op de initialisatie"
countdown 10
echo "opzetten SSH connectie met private key authentication"
ssh -q -o StrictHostKeyChecking=no -o BatchMode=yes -i ./openssh-server/key test@openssh_test -p 2222 "echo 2>&1" && status="success" || status="failed"
echo "Verbinden is geeindigd met status: $status"
echo "test docker container openssh_test wordt nu verwijderd"
docker rm openssh_test -f
echo "opruimen test keys"
rm ./openssh-server/key
rm ./openssh-server/key.pub

if [ "$status" == "success" ]; 
   then
      echo "Image test successvol. We eindigen met errorlevel 0"
      set_return 0
   else
      echo "Image test onsuccessvol. We eindigen met errorlevel 1"
      set_return 1
fi
