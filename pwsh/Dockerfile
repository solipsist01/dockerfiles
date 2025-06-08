# Use a stable Debian base
FROM debian:bullseye-slim

# Install required dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        gnupg \
        apt-transport-https \
        software-properties-common && \
    rm -rf /var/lib/apt/lists/*

# Import the Microsoft GPG key and add PowerShell repository
RUN curl -sSL https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor > microsoft.gpg && \
    install -o root -g root -m 644 microsoft.gpg /etc/apt/trusted.gpg.d/ && \
    sh -c 'echo "deb [arch=amd64] https://packages.microsoft.com/debian/11/prod bullseye main" > /etc/apt/sources.list.d/microsoft.list' && \
    rm microsoft.gpg

# Install PowerShell
RUN apt-get update && \
    apt-get install -y powershell && \
    rm -rf /var/lib/apt/lists/*

# Set pwsh as the default command
ENTRYPOINT ["pwsh"]
