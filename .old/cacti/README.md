﻿# Cacti v1+ Docker Container
[![](https://images.microbadger.com/badges/image/smcline06/cacti.svg)](https://microbadger.com/images/smcline06/cacti "Get your own image badge on microbadger.com")

##### Github Repo: https://github.com/scline/docker-cacti
##### Dockerhub Repo: https://hub.docker.com/r/smcline06/cacti/

## Cacti System
Cacti is a complete network graphing solution designed to harness the power of RRDTool's data storage and graphing functionality. Cacti provides following features:

* remote and local data collectors
* network discovery
* device management automation
* graph templating
* custom data acquisition methods
* user, group and domain management
* C3 level security settings for local accounts
  * strong password hashing
  * forced regular password changes, complexity, and history 
  * account lockout support

All of this is wrapped in an intuitive, easy to use interface that makes sense for both LAN-sized installations and complex networks with thousands of devices.
Developed in the early 2000's by Ian Berry as a high school project, it has been used by thousands of companies and enthusiasts to monitor and manage their Networks and Data Centers.
More information around this opensource product can be located at the following [website][cws].

## Using this image
### Running the container
This container contains Cacti v1+ and is not compatible with older version of cacti. It does rely on an external MySQL database that can be already configured before initial startup or having the container itself perform the setup and initialization. If you want this container to perform these steps for you, you will need to pass the root password for mysql login or startup will fail. This container automatically incorporates Cacti Spine's multithreaded poller.

### Exposed Ports
The following ports are important and used by Cacti

| Port |     Notes     |  
|------|:-------------:|
|  80  | HTTP GUI Port |
|  443 | HTTPS GUI Port|

It is recommended to allow at least one of the above ports for access to the monitoring system. This is translated by the -p hook. For example
`docker run -p 80:80 -p 443:443`

## Installation

### Cacti Master
The main cacti poller settings, these are required for single cacti and multi cacti host installations.

| Environment Variable | Function |
|---|---|
| DB_NAME | The MySQL database name, this is used for both cacti settings and spine poller configurations. |
| DB_USER | MySQL database user cacti should use. Both cacti and spine poller will share these settings. |
| DB_PASS | MySQL database password assigned to `DB_USER` Both cacti and spine poller will share these settings. |
| DB_HOST | The IP address, FQDN/hostname, or linked container name that cacti would use as a database. |
| DB_PORT | What TCP port is the MySQL database listening on, by default its 3306. |
| DB_ROOT_PASS | This is only needed  if the `INITIALIZE_DB` is set to 1. This is required if you want the cacti container to setup remote MySQL user accounts and Databases for use. |
| INITIALIZE_DB | Can be `0` for false or `1` for true. If true the container will require `DB_ROOT_PASS` to the target database. The container will attempt to create usernames/passwords and Databases required on the remote system for Cacti to funtion. |
| TZ | TimeZone, please select a format Centos understands, a list can be generated by running `ls /usr/share/zoneinfo`. |
| BACKUP_RETENTION | Number of backup files to keep. |
| BACKUP_TIME | How often Cacti should back itself up in minutes - currently not working |
| REMOTE_POLLER | Can be `0` for false (default) or `1` for true. |

### Remote Cacti Pollers
Remote cacti poller containers require the following, the major differance here is the inclusion of RDB (remote database) variables which should be pointed at the master cacti installation settings. 

| Environment Variable | Function |
|---|---|
| DB_NAME | The MySQL database, this is used for both cacti settings and spine poller configurations. | 
| DB_USER | MySQL database user cacti should use. Both cacti and spine poller will share these settings. | 
| DB_PASS | MySQL database password assigned to `DB_USER` Both cacti and spine poller will share these settings. | 
| DB_HOST | The IP address, FQDN/hostname, or linked container name that cacti would use as a database. | 
| DB_PORT | What TCP port is the MySQL database listening on, by default its 3306. | 
| INITIALIZE_DB | Can be `0` for false or `1` for true. If true the container will require `DB_ROOT_PASS` to the target database. The container will attempt to create usernames/passwords and Databases required on the remote system for Cacti to funtion. |
| TZ | TimeZone, please select a format Centos understands, a list can be generated by running `ls /usr/share/zoneinfo`.|
| BACKUP_RETENTION | Number of backup files to keep|
| BACKUP_TIME | How often Cacti should back itself up in minutes - currently not working |
| REMOTE_POLLER | Can be `0` for false (default) or `1` for true. If true the container is setup as a remote poller. |
| RDB_NAME | The master Cacti instance MySQL database name, this is used for both cacti settings and spine poller configurations. | 
| RDB_USER | MySQL database user used by the master Cacti container should use. | 
| RDB_PASS | MySQL database password assigned to `RDB_USER` that is used by the master Cacti container. | 
| RDB_HOST | The IP address, FQDN/hostname, or linked container name that the master Cacti instance uses | 
| RDB_PORT | What TCP port is the MySQL database listening on, by default its 3306. | 

### Database Settings
The folks at Cacti.net recommend the following settings for its MySQL based database. Please understand depending on your systems resources and amount of devices your installation is monitoring these settings may need to change for optimal performance. I would recommend shooting any questions around these settings to the [Cacti community forums][cacti_forums].

|MySQL Variable| Recommended Value | Notes |
|---|---|---|
| Version | >= 5.6 | MySQL 5.6+ and MariaDB 10.0+ are great releases, and are very good versions to choose. Make sure you run the very latest release though which fixes a long standing low level networking issue that was casuing spine many issues with reliability. |
| collation_server | utf8mb4_unicode_ci | When using Cacti with languages other than English, it is important to use the utf8mb4_unicode_ci collation type as some characters take more than a single byte. |
| character_set_client | utf8mb4 | When using Cacti with languages other than English, it is important ot use the utf8mb4 character set as some characters take more than a single byte. |
| max_connections | >= 100 | Depending on the number of logins and use of spine data collector, MySQL will need many connections. The calculation for spine is: total_connections = total_processes * (total_threads + script_servers + 1), then you must leave headroom for user connections, which will change depending on the number of concurrent login accounts. |
| max_heap_table_size | >= 10% RAM |  If using the Cacti Performance Booster and choosing a memory storage engine, you have to be careful to flush your Performance Booster buffer before the system runs out of memory table space. This is done two ways, first reducing the size of your output column to just the right size. This column is in the tables poller_output, and poller_output_boost. The second thing you can do is allocate more memory to memory tables. We have arbitrarily chosen a recommended value of 10% of system memory, but if you are using SSD disk drives, or have a smaller system, you may ignore this recommendation or choose a different storage engine. You may see the expected consumption of the Performance Booster tables under Console -> System Utilities -> View Boost Status.|
| max_allowed_packet | >= 16777216 | With Remote polling capabilities, large amounts of data will be synced from the main server to the remote pollers. Therefore, keep this value at or above 16M. |
| tmp_table_size | >= 64M | When executing subqueries, having a larger temporary table size, keep those temporary tables in memory. |
| join_buffer_size | >= 64M | When performing joins, if they are below this size, they will be kept in memory and never written to a temporary file. |
| innodb_file_per_table | ON | When using InnoDB storage it is important to keep your table spaces separate. This makes managing the tables simpler for long time users of MySQL. If you are running with this currently off, you can migrate to the per file storage by enabling the feature, and then running an alter statement on all InnoDB tables. |
| innodb_buffer_pool_size | >=25% RAM | InnoDB will hold as much tables and indexes in system memory as is possible. Therefore, you should make the innodb_buffer_pool large enough to hold as much of the tables and index in memory. Checking the size of the /var/lib/mysql/cacti directory will help in determining this value. We are recommending 25% of your systems total memory, but your requirements will vary depending on your systems size. |
| innodb_doublewrite | OFF  | With modern SSD type storage, this operation actually degrades the disk more rapidly and adds a 50% overhead on all write operations. |
| innodb_lock_wait_timeout | >= 50 | Rogue queries should not for the database to go offline to others. Kill these queries before they kill your system. |
| innodb_flush_log_at_timeout | >= 3 |  As of MySQL 5.7.14-8, the you can control how often MySQL flushes transactions to disk. The default is 1 second, but in high I/O systems setting to a value greater than 1 can allow disk I/O to be more sequential |
| innodb_read_io_threads | >= 32 | With modern SSD type storage, having multiple read io threads is advantageous for applications with high io characteristics. |
| innodb_write_io_threads | >= 16 | With modern SSD type storage, having multiple write io threads is advantageous for applications with high io characteristics. |

### Data Backups
Included is a backup script that will backup cacti (including settings/plugins), rrd files, and spine. This is accomplished by taking a complete copy of the root spine and cacti directory and performing a MySQL dump of the cacti database which stores all the settings and device information. To manually perform a backup, run the following exec commands: 

```
docker exec <docker image ID or name> ./backup.sh
```

This will store compressed backups in a tar.gz format within the cacti docker container under /backups directory. Its recommended to map this directory using volumes so data is persistent. By default it only stores 7 most recent backups and will automatically delete older ones, to change this value update `BACKUP_RETENTION` environmental variable with the number of backups you wish to store.

### Restore Backup
To restore from an existing backup, run the following docker exec command with the backup file location as an argument.
```
docker exec <docker image ID or name> ./restore.sh /backup/<filename>
```

To get a list of backups, the following command should display them:
```
docker exec <docker image ID or name> ls /backup
```

### Updating Cacti
You can now update the Cacti/Spine version of this container using the included script. By default this will update to the *latest* version. 
```
docker exec <docker image ID or name> ./upgrade.sh
```

If you want to specify a specific version please update the `/upgrade.sh` values.
 * [Cacti Version Links][cacti_download]
 * [Spine Version Links][spine_download]

```
#!/bin/bash
# script to upgrade a cacti instance to latest, if you want a specific version please update the following download links
cacti_download_url=http://www.cacti.net/downloads/cacti-latest.tar.gz
spine_download_url=http://www.cacti.net/downloads/spine/cacti-spine-latest.tar.gz
``` 

## Docker Cacti Architecture
-----------------------------------------------------------------------------
With the recent update to version 1+, Cacti has introduced the ability to have remote polling servers. This allows us to have one centrally located UI and information system while scaling out multiple datacenters or locations. Each instance, master or remote poller, requires its own MySQL based database. The pollers also have an addition requirement to access the Cacti master's database with read/write access.

Some docker-compose examples can be found in the following [readme][arch]

## Container Customization
There are a few customizations you can do if you are building the container locally. During the build process Plugins and Device Templates can be added to folders where at startup, scripts will import and install.

### Device Templates
Dropping device templates in the `/templates/` folder using the following structure:
```
├── templates
│   ├── template_name.xml
│   ├── resource
│   │   └── script_queries
│   │       └── ...
│   │   └── script_server
│   │       └── ...
│   │   └── snmp_queries
│   │       └── ...
│   ├── scripts
│   │   └── ...
```
At buld/first boot you will see some log messages that they were imported to the underlying Cacti system
```
2017-03-24_19:22 [New Install] Installing supporting template files.
2017-03-24_19:22 [New Install] Installing template file /templates/cacti_host_template_juniper_networks.xml
```

### Plugins
To have plugins automatically loaded on boot, simply have the uncompressed plugin in the `plugins` folder within the main directory. Upon build/run, the startup script will automatically install them to the appropriate directory. Please understand that you will need to enable any plugins via Cacti GUI for them to become active. 

To add plugins after the container is built, for example if pulling directly form dockerhub, mount the `/cacti/plugins` directory using [docker volumes][docker_volume_help]. 

### Settings
Settings can be passed through to cacti at initial install by placing the SQL changes in the form of filename.sql under the settings folder. start.sh will automatically merge all *.sql files during install. For example the folling is there to enable spine by default:
##### /settings/spine.sql
```
--
-- Enable spine poller from docker installation
--

REPLACE INTO `%DB_NAME%`.`settings` (`name`, `value`) VALUES('path_spine', '/spine/bin/spine');
REPLACE INTO `%DB_NAME%`.`settings` (`name`, `value`) VALUES('path_spine_config', '/spine/etc/spine.conf');
REPLACE INTO `%DB_NAME%`.`settings` (`name`, `value`) VALUES('poller_type', '2');
```

# Change Log
#### 1.1.38 - 05/12/2018
 * Update Cacti and Spine from 1.1.37 to 1.1.38
   * [changelog 1.1.37 -> 1.1.38][CL1.1.38]
 * Merge yum run commands in dockerfile to reduce stored space.

#### 1.1.37 - 04/4/2018
 * Update Cacti and Spine from 1.1.34 to 1.1.37
   * [changelog 1.1.36 -> 1.1.37][CL1.1.37]
   * [changelog 1.1.35 -> 1.1.36][CL1.1.36]
   * [changelog 1.1.34 -> 1.1.35][CL1.1.35]
 * Close Issue [#36](https://github.com/scline/docker-cacti/issues/36) - Initialize DB fails if mysql running on non-standard port
 * Close Issue [#38](https://github.com/scline/docker-cacti/issues/38) - "httpd: Could not reliably determine the server's fully qualified domain name" httpd errors
 * Close Issue [#40](https://github.com/scline/docker-cacti/issues/40) - Remove documentation about automated backups since this is not implemented. 


#### 1.1.34 - 02/8/2018
 * Update Cacti and Spine from 1.1.31 to 1.1.34
   * [changelog 1.1.33 -> 1.1.34][CL1.1.34]
   * [changelog 1.1.32 -> 1.1.33][CL1.1.33]
   * [changelog 1.1.31 -> 1.1.32][CL1.1.32]

#### 1.1.31 - 01/18/2018
 * Update Cacti and Spine from 1.1.30 to 1.1.31
   * [changelog 1.1.30 -> 1.1.31][CL1.1.31]

#### 1.1.30 - 01/03/2018
 * Update Cacti and Spine from 1.1.28 to 1.1.30
   * [changelog 1.1.29 -> 1.1.30][CL1.1.30]
   * [changelog 1.1.28 -> 1.1.29][CL1.1.29]

#### 1.1.28u1 - 12/23/2017
 * Removed pre-installed plugins (expecting users to add there own)
 * Refactored the way Cacti is installed. This is now removed from Dockerfile and moved to start.sh
   * Allows the volume mounting of '/cacti', before this would break cacti installation

#### 1.1.28 - 11/21/2017
 * Update Cacti and Spine from 1.1.27 to 1.1.28
   * [changelog 1.1.27 -> 1.1.28][CL1.1.28]

#### 1.1.27 - 11/07/2017
 * Update Cacti and Spine from 1.1.24 to 1.1.27
   * [changelog 1.1.26 -> 1.1.27][CL1.1.27]
   * [changelog 1.1.25 -> 1.1.26][CL1.1.26]
   * [changelog 1.1.24 -> 1.1.25][CL1.1.25]

#### 1.1.24 - 09/18/2017
 * Update Cacti and Spine from 1.1.19 to 1.1.24 
   * [changelog 1.1.23 -> 1.1.24][CL1.1.24]
   * [changelog 1.1.22 -> 1.1.23][CL1.1.23]
   * [changelog 1.1.21 -> 1.1.22][CL1.1.22]
   * [changelog 1.1.20 -> 1.1.21][CL1.1.21]
   * [changelog 1.1.19 -> 1.1.20][CL1.1.20]

#### 1.1.19 - 08/21/2017
 * Update Cacti and Spine from 1.1.12 to 1.1.19 
   * [changelog 1.1.18 -> 1.1.19][CL1.1.19]
   * [changelog 1.1.17 -> 1.1.18][CL1.1.18]
   * [changelog 1.1.16 -> 1.1.17][CL1.1.17]
   * [changelog 1.1.15 -> 1.1.16][CL1.1.16]
   * [changelog 1.1.14 -> 1.1.15][CL1.1.15]
   * [changelog 1.1.13 -> 1.1.14][CL1.1.14]
   * [changelog 1.1.12 -> 1.1.13][CL1.1.13]

#### 1.1.12 - 07/05/2017
 * Update Cacti and Spine from 1.1.11 to 1.1.12 - [changelog link][CL1.1.12]
 * Update upgrade.sh script to use `wget` instead of `curl` due to URL errors.
 
#### 1.1.11 - 07/04/2017
 * Update Cacti and Spine from 1.1.10 to 1.1.11 - [changelog link][CL1.1.11]

#### 1.1.10 - 06/17/2017
 * Update Cacti and Spine from 1.1.9 to 1.1.10 - [changelog link][CL1.1.10]

#### 1.1.9 - 06/08/2017
 * Update Cacti and Spine from 1.1.5 to 1.1.9 
   * [changelog 1.1.8 -> 1.1.9][CL1.1.9]
   * [changelog 1.1.7 -> 1.1.8][CL1.1.8]
   * [changelog 1.1.6 -> 1.1.7][CL1.1.7]
   * [changelog 1.1.5 -> 1.1.6][CL1.1.6]
 * Update cacti plugins
   * thold from 1.0.2 -> 1.0.3
   * monitor from 2.0 -> 2.1
   * syslog from 2.0 -> 2.1

#### 1.1.5 - 04/27/2017
 * Update Cacti and Spine from 1.1.4 to 1.1.5 - [changelog link][CL1.1.5]

#### 1.1.4 - 04/24/2017
 * Update Cacti and Spine from 1.1.3 to 1.1.4 - [changelog link][CL1.1.4]
 * Update THOLD template with master due to function bug on cacti 1.1+

#### 1.1.3 - 04/15/2017
 * Update Cacti and Spine from 1.1.2 to 1.1.3 - [changelog link][CL1.1.3]
 * remove temp automation_api file fix since this has been solved in 1.1.3

#### 1.1.2 - 04/11/2017
 * Added 1 Minute polling template
 * Updated plugin THOLD 1.0.1 -> 1.0.2
 * Updated CereusTransporter 0.65 -> 0.66
 * Added F5, ESX, PerconaDB, and Linux host templates
##### --- 04/09/2017 ---
 * Update crontab from apache user to /etc/crontab
 * Apply https://github.com/CentOS/CentOS-Dockerfiles/issues/31 fix so cron works on Centos:7 container
##### --- 04/02/2017 ---
 * Update Cacti and Spine from 1.1.1 to 1.1.2 - [changelog link][CL1.1.2]
 * Restore from a cacti backup is now working via `restore.sh <backupfile>` command
 * Minor cleanup of `backup.sh` script
 * Upgrade cacti script created and tested using `upgrade.sh` script
 
#### 1.1.1 - 03/27/2017
 * Update Cacti and Spine from 1.1.0 to 1.1.1 - [changelog link][CL1.1.1]
 * GitHub ReadMe organization

#### 1.1.0 - 03/25/2017
 * Initial push

# Known Issues/Fixes
* HTTPS is not setup to work, it may work just understand no testing has been done.

# ToDo
* Auto import remote pollers, currently you need to navigate to there GUI for a few clicks.
* Documentation cleanup.

[CL1.1.38]: http://www.cacti.net/release_notes.php?version=1.1.38
[CL1.1.37]: http://www.cacti.net/release_notes.php?version=1.1.37
[CL1.1.36]: http://www.cacti.net/release_notes.php?version=1.1.36
[CL1.1.35]: http://www.cacti.net/release_notes.php?version=1.1.35
[CL1.1.34]: http://www.cacti.net/release_notes.php?version=1.1.34
[CL1.1.33]: http://www.cacti.net/release_notes.php?version=1.1.33
[CL1.1.32]: http://www.cacti.net/release_notes.php?version=1.1.32
[CL1.1.31]: http://www.cacti.net/release_notes.php?version=1.1.31
[CL1.1.30]: http://www.cacti.net/release_notes.php?version=1.1.30
[CL1.1.29]: http://www.cacti.net/release_notes.php?version=1.1.29
[CL1.1.28]: http://www.cacti.net/release_notes.php?version=1.1.28
[CL1.1.27]: http://www.cacti.net/release_notes.php?version=1.1.27
[CL1.1.26]: http://www.cacti.net/release_notes.php?version=1.1.26
[CL1.1.25]: http://www.cacti.net/release_notes.php?version=1.1.25
[CL1.1.24]: http://www.cacti.net/release_notes.php?version=1.1.24
[CL1.1.23]: http://www.cacti.net/release_notes.php?version=1.1.23
[CL1.1.22]: http://www.cacti.net/release_notes.php?version=1.1.22
[CL1.1.21]: http://www.cacti.net/release_notes.php?version=1.1.21
[CL1.1.20]: http://www.cacti.net/release_notes.php?version=1.1.20
[CL1.1.19]: http://www.cacti.net/release_notes.php?version=1.1.19
[CL1.1.18]: http://www.cacti.net/release_notes.php?version=1.1.18
[CL1.1.17]: http://www.cacti.net/release_notes.php?version=1.1.17
[CL1.1.16]: http://www.cacti.net/release_notes.php?version=1.1.16
[CL1.1.15]: http://www.cacti.net/release_notes.php?version=1.1.15
[CL1.1.14]: http://www.cacti.net/release_notes.php?version=1.1.14
[CL1.1.13]: http://www.cacti.net/release_notes.php?version=1.1.13
[CL1.1.12]: http://www.cacti.net/release_notes.php?version=1.1.12
[CL1.1.11]: http://www.cacti.net/release_notes.php?version=1.1.11
[CL1.1.10]: http://www.cacti.net/release_notes.php?version=1.1.10
[CL1.1.9]: http://www.cacti.net/release_notes.php?version=1.1.9
[CL1.1.8]: http://www.cacti.net/release_notes.php?version=1.1.8
[CL1.1.7]: http://www.cacti.net/release_notes.php?version=1.1.7
[CL1.1.6]: http://www.cacti.net/release_notes.php?version=1.1.6
[CL1.1.5]: http://www.cacti.net/release_notes.php?version=1.1.5
[CL1.1.4]: http://www.cacti.net/release_notes.php?version=1.1.4
[CL1.1.3]: http://www.cacti.net/release_notes_1_1_3.php
[CL1.1.2]: http://www.cacti.net/release_notes_1_1_2.php
[CL1.1.1]: http://www.cacti.net/release_notes_1_1_1.php
[cacti_download]: http://www.cacti.net/downloads
[spine_download]: http://www.cacti.net/downloads/spine
[arch]: https://github.com/scline/docker-cacti/tree/master/docker-compose
[docker_volume_help]: https://docs.docker.com/engine/tutorials/dockervolumes
[cacti_forums]: http://forums.cacti.net
[cws]: http://cacti.net