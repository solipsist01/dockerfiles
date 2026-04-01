FROM devolutions/devolutions-gateway

COPY /customization.ps1 /usr/local/bin/customization.ps1

ENTRYPOINT ["pwsh", "-File", "/usr/local/bin/customization.ps1"]