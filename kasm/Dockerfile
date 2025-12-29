#FROM kasmweb/desktop:1.18.0-rolling-weekly
FROM kasmweb/core-debian-bookworm:1.18.0-rolling-weekly
USER root

ENV HOME /home/kasm-default-profile
ENV STARTUPDIR /dockerstartup
ENV INST_SCRIPTS $STARTUPDIR/install
WORKDIR $HOME

######### Customize Container Here ###########
#https://kasmweb.com/docs/latest/how_to/building_images.html
RUN apt-get update && apt-get install -y apt-transport-https mc \
    traceroute iputils-ping wget xz-utils curl doublecmd-gtk dnsutils \
    software-properties-common soundconverter python3-full python3-pip sudo vlc \
    firefox-esr mkvtoolnix mkvtoolnix-gui handbrake

#torclient
RUN mkdir /apps && \
    TOR_URL=$(curl -q https://www.torproject.org/download/ | \
        grep downloadLink | \
        grep linux | \
        sed 's/.*href="//g' | \
        cut -d '"' -f1 | \
        head -1) && \
    FULL_TOR_URL="https://www.torproject.org/${TOR_URL}" && \
    wget --quiet "${FULL_TOR_URL}" -O /tmp/torbrowser.tar.xz && \
    tar -xJf /tmp/torbrowser.tar.xz -C /apps && \
    rm /tmp/torbrowser.tar.xz && \
    chown 1000:0 /apps -R && \
    sed -i 's/^Name=.*/Name=Tor Browser/g' /apps/tor-browser/start-tor-browser.desktop && \
    sed -i 's/Icon=.*/Icon=\/usr\/share\/icons\/tor.png/g' /apps/tor-browser/start-tor-browser.desktop && \
    sed -i 's/Exec=.*/Exec=sh -c \x27"\/apps\/tor-browser\/Browser\/start-tor-browser" --detach || ([ !  -x "\/apps\/tor-browser\/Browser\/start-tor-browser" ] \&\& "$(dirname "$*")"\/Browser\/start-tor-browser --detach)\x27 dummy %k/g' /apps/tor-browser/start-tor-browser.desktop && \
    mkdir -p /home/kasm-user/.local/share/applications && \
    cp /apps/tor-browser/start-tor-browser.desktop /home/kasm-user/.local/share/applications/tor-browser.desktop && \
    chown 1000:1000 /home/kasm-user/.local/share/applications/tor-browser.desktop && \
    chmod 644 /home/kasm-user/.local/share/applications/tor-browser.desktop && \
    cp /apps/tor-browser/Browser/browser/chrome/icons/default/default128.png /usr/share/icons/tor.png && \
    chown 1000:0 /usr/share/icons/tor.png

RUN wget -U "Mozilla" https://downloads.exodus.com/releases/exodus-linux-x64-25.46.7.deb -O /tmp/exodus.deb && \
    dpkg -i /tmp/exodus.deb || apt-get install -yf && rm /tmp/exodus.deb

RUN pip3 install unidecode --break-system-packages

######### End Customizations ###########
RUN echo "kasm-user ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/kasm-user && chmod 0440 /etc/sudoers.d/kasm-user

RUN chown 1000:0 $HOME 
RUN $STARTUPDIR/set_user_permission.sh $HOME

ENV HOME /home/kasm-user
WORKDIR $HOME
RUN mkdir -p $HOME && chown -R 1000:0 $HOME 
USER 1000