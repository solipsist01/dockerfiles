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
echo "Starten van openssh_test docker container voor test"
docker run -d --name=openssh_test -e PUID=1000 -e PGID=1000 -e TZ=europe/amsterdam -e PUBLIC_KEY='ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQCsIBoZs4NSPg8og/zNECKqT6eIO7H1pZb06PhB/Hf+qieoFJkMfu1DWAMJY6XgthDha5pLlSr/AZqkIS6xUAuM4W6KAskvm4XzmnP9S9i04TsE4s+XrpOS0lZHawXsTmShVo2W1skqfJucos2p+35n9yjvmfDGxgPJJmKDDTD9ngLjmNjarW1OdIvB99Os1pxnqg5ZgFmQvJkpO+dV8WxU6SU8FF8B/F0TWviv9g+a67Fm+2cZJXDdCkxJHk27vZjCJCVZcYFcQmCOHwRULTaq8PbEC3Pnk8bPuA9Cg7gQPrWoa5VQ1OQWXDT/ny5f4a8qNfBw5VnjByMMBsn3g//J test@openssh_test' -e SUDO_ACCESS=false -e PASSWORD_ACCESS=false -e USER_NAME=test -l traefik.enable=false solipsist01/openssh-server
echo "wachten op de initialisatie"
countdown 10
echo "rechten aanpassen van test private key"
chmod 600 ./openssh-server/testkey_id_rsa
echo "opzetten SSH connectie met private key authentication"
ping openssh_test
ssh -q -o StrictHostKeyChecking=no -o BatchMode=yes -i ./openssh-server/testkey_id_rsa test@openssh_test -p 2222 "echo 2>&1" && status="success" || status="failed"
echo "Verbinden is geeindigd met status: $status"
echo "test docker container openssh_test wordt nu verwijderd"
docker rm openssh_test -f

if [ "$status" == "success" ]; 
   then
      echo "Image test successvol. We eindigen met errorlevel 0"
      set_return 0
   else
      echo "Image test onsuccessvol. We eindigen met errorlevel 1"
      set_return 1
fi
