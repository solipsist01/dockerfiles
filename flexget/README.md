# flexget
Flexget Dockerfile for automated Docker builds

How to use this image:

1. Create an account on NPO.nl
2. Create a config.yml file in your config directory

```
tasks:
  npo_task:
    npo_watchlist:
      email: Your@NPO_Account_mail_address.com
      password: Y0urUB4hdstr0nP44w0rd
      remove_accepted: yes
      max_episode_age_days: 999
    accept_all: yes
    seen:
      fields:
        - url
    manipulate:
      - title:
          replace:
            regexp: '( \(\D*_\d*\))'
            format: ''
      - title:
          replace: 
            regexp: '(\:)' 
            format: ''
      - title:          
          replace: 
            regexp: '(\\)' 
            format: '-'
      - title:          
          replace: 
            regexp: '(\/)' 
            format: '-'            
    exec:
      fail_entries: yes
      auto_escape: yes
      on_output:
        for_accepted:
          - date >>/flexget/history.txt && echo "{{series_name_plain}} - {{series_date | formatdate("%Y-%m-%d") }} - {{title}} \n" >>/flexget/history.txt && youtube-dl -o "/output/{{series_name_plain}}/{{series_name_plain}} - {{series_date | formatdate("%Y-%m-%d") }} - {{title}}.%(ext)s" -f bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4] {{url}} 
```

run the docker container

```
docker run 
--name=flexget 
-d 
-e TZ=Europe/Amsterdam 
-v <Your config folder>:/flexget 
-v <Your temporary folder>:/input 
-v <Your download folder>:/output 
solipsist01/flexget
```

This image checks npo.nl every 1 hour. When you launch the container, you have to wait for an hour before it starts downloading.
If you want to force it to download run:

docker exec -it flexget flexget execute

