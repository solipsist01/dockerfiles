#!/usr/bin/python3
import docker
import argparse
import shutil
import signal
import time
import sys
import os

label_name = "hoster.domains"
enclosing_pattern = "#-----------Docker-Hoster-Domains----------\n"
hosts_path = "/tmp/hosts"
hosts = {}

def signal_handler(signal, frame):
    global hosts
    hosts = {}
    update_hosts_file()
    sys.exit(0)

def main():
    # register the exit signals
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    args = parse_args()
    global hosts_path
    hosts_path = args.file

    dockerClient = docker.APIClient(base_url='unix://%s' % args.socket)
    events = dockerClient.events(decode=True)
    #get running containers
    for c in dockerClient.containers(quiet=True, all=False):
        container_id = c["Id"]
        container = get_container_data(dockerClient, container_id)
        hosts[container_id] = container

    update_hosts_file()

    #listen for events to keep the hosts file updated
    for e in events:
        if e.get("Type") != "container":
            continue

        status = e.get("status")
        if not status:
            continue  # пропускаем события без статуса

        container_id = e.get("id")
        if not container_id:
            continue
        
        status = e["status"]
        if status =="start":
            container_id = e["id"]
            container = get_container_data(dockerClient, container_id)
            hosts[container_id] = container
            update_hosts_file()

        if status=="stop" or status=="die" or status=="destroy":
            container_id = e["id"]
            if container_id in hosts:
                hosts.pop(container_id)
                update_hosts_file()

        if status=="rename":
            container_id = e["id"]
            if container_id in hosts:
                container = get_container_data(dockerClient, container_id)
                hosts[container_id] = container
                update_hosts_file()


def get_container_data(dockerClient, container_id):
    # extract all the info with the docker api
    info = dockerClient.inspect_container(container_id)

    # normalise basic names
    container_hostname = info.get("Config", {}).get("Hostname", "") or ""
    container_name = (info.get("Name", "") or "").lstrip("/")

    # prefer old-style IP if present (compatibility), otherwise try networks
    network_settings = info.get("NetworkSettings", {}) or {}
    container_ip_fallback = network_settings.get("IPAddress")  # may be '' or None

    # full hostname with domainname if present
    domainname = info.get("Config", {}).get("Domainname") or ""
    if domainname:
        container_hostname = container_hostname + "." + domainname

    result = []

    # iterate all networks (new API puts IPs here)
    networks = network_settings.get("Networks") or {}
    for net_name, net_vals in networks.items():
        if not net_vals:
            continue
        ip = net_vals.get("IPAddress") or None
        aliases = net_vals.get("Aliases") or []  # could be None
        # if there is no ip, skip this network
        if not ip:
            continue

        domains = set()
        # include aliases if present
        if aliases:
            # aliases may include None or empty strings — filter them
            domains.update([a for a in aliases if a])
        # include container name and hostname
        if container_name:
            domains.add(container_name)
        if container_hostname:
            domains.add(container_hostname)

        result.append({
            "ip": ip,
            "name": container_name,
            "domains": domains
        })

    # if old-style IP exists and not already present in result, add it
    if container_ip_fallback:
        # check we don't duplicate the same IP already collected from networks
        ip_already = any(r.get("ip") == container_ip_fallback for r in result)
        if not ip_already:
            result.append({
                "ip": container_ip_fallback,
                "name": container_name,
                "domains": {container_name, container_hostname} if container_hostname else {container_name}
            })

    return result


def update_hosts_file():
    if len(hosts)==0:
        print("Removing all hosts before exit...")
    else:
        print("Updating hosts file with:")

    for id,addresses in hosts.items():
        for addr in addresses:
            print("ip: %s domains: %s" % (addr["ip"], addr["domains"]))

    #read all the lines of thge original file
    lines = []
    with open(hosts_path,"r+") as hosts_file:
        lines = hosts_file.readlines()

    #remove all the lines after the known pattern
    for i,line in enumerate(lines):
        if line==enclosing_pattern:
            lines = lines[:i]
            break;

    #remove all the trailing newlines on the line list
    if lines:
        while lines[-1].strip()=="": lines.pop()

    #append all the domain lines
    if len(hosts)>0:
        lines.append("\n\n"+enclosing_pattern)
        
        for id, addresses in hosts.items():
            for addr in addresses:
                lines.append("%s    %s\n"%(addr["ip"],"   ".join(addr["domains"])))
        
        lines.append("#-----Do-not-add-hosts-after-this-line-----\n\n")

    #write it on the auxiliar file
    aux_file_path = hosts_path+".aux"
    with open(aux_file_path,"w") as aux_hosts:
        aux_hosts.writelines(lines)

    #replace etc/hosts with aux file, making it atomic
    shutil.move(aux_file_path, hosts_path)


def parse_args():
    parser = argparse.ArgumentParser(description='Synchronize running docker container IPs with host /etc/hosts file.')
    parser.add_argument('socket', type=str, nargs="?", default="/tmp/docker.sock", help='The docker socket to listen for docker events.')
    parser.add_argument('file', type=str, nargs="?", default="/tmp/hosts", help='The /etc/hosts file to sync the containers with.')
    return parser.parse_args()

if __name__ == '__main__':
    main()
