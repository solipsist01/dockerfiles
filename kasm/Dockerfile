FROM kasmweb/ubuntu-jammy-desktop:1.13.0-rolling
USER root

ENV HOME /home/kasm-default-profile
ENV STARTUPDIR /dockerstartup
ENV INST_SCRIPTS $STARTUPDIR/install
WORKDIR $HOME

######### Customize Container Here ###########
#https://kasmweb.com/docs/latest/how_to/building_images.html
RUN apt-get update  && apt-get install -y flatpak apt-transport-https mc \
    traceroute iputils-ping wget xz-utils curl doublecmd-gtk dnsutils \
    software-properties-common  soundconverter python3-pip python3-pyparsing python3-mutagen \
    python3-pyqt5 python3-configobj python3-pyqt5.qtsvg libapr1 libaprutil1 libxcb-cursor0 sudo \
    vlc kdenlive intel-gpu-tools

#flatpak
RUN flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo \
    && flatpak install flathub \ 
    com.moonlight_stream.Moonlight \ 
    fr.handbrake.ghb \
    no.mifi.losslesscut \
    org.bunkus.mkvtoolnix-gui \
    com.makemkv.MakeMKV \
    org.remmina.Remmina \
    org.kde.subtitlecomposer \
    org.remmina.Remmina \
    com.github.micahflee.torbrowser-launcher \
    --noninteractive \
    && cp /var/lib/flatpak/exports/share/applications/*.desktop $HOME/Desktop 


#native
RUN  mkdir /software && mkdir /software/puddletag && git clone https://github.com/puddletag/puddletag /software/puddletag \
    && chmod +x /software/puddletag/puddletag \
    && DESKTOPITEM="$HOME/Desktop/puddletag.desktop" \
    && echo "[Desktop Entry]" > $DESKTOPITEM \
    && echo "Version=1.0" >> $DESKTOPITEM \
    && echo "Type=Application" >> $DESKTOPITEM \
    && echo "Name=puddletag" >> $DESKTOPITEM \
    && echo "TryExec=/software/puddletag/puddletag" >> $DESKTOPITEM \
    && echo "Exec=/software/puddletag/puddletag %F" >> $DESKTOPITEM \
    && echo "Icon=/software/puddletag/puddletag.png" >> $DESKTOPITEM \
    && echo "GenericName=Audio Tag Editor" >> $DESKTOPITEM \
    && echo "Categories=AudioVideo;Audio;Qt;" >> $DESKTOPITEM \
    && chmod +x $DESKTOPITEM && chown 1000:0 $DESKTOPITEM \
    && chown 1000:0 /software -R

#icons
RUN cp /usr/share/applications/doublecmd.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/exo-terminal-emulator.desktop $HOME/Desktop/ \
    && chmod +x $HOME/Desktop/doublecmd.desktop \
    && chmod +x $HOME/Desktop/exo-terminal-emulator.desktop \
    && chown 1000:1000 $HOME/Desktop/doublecmd.desktop \
    && chown 1000:1000 $HOME/Desktop/exo-terminal-emulator.desktop \
    && sed -i 's/%U/www.wenro.nl\/startpage/g' $HOME/Desktop/google-chrome.desktop \
    && echo 'pref("browser.startup.homepage_override.mstone", "ignore");' >> /usr/lib/firefox/browser/defaults/preferences/syspref.js \
    && echo 'pref("browser.startup.homepage", "http://www.wenro.nl/startpage");' >> /usr/lib/firefox/browser/defaults/preferences/syspref.js


RUN wget https://raw.githubusercontent.com/solipsist01/dockerfiles/master/kasm/bg_default.png -O /usr/share/extra/backgrounds/bg_default.png \
    && echo 'kasm-user ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

######### End Customizations ###########

RUN chown 1000:0 $HOME 
RUN $STARTUPDIR/set_user_permission.sh $HOME

ENV HOME /home/kasm-user
WORKDIR $HOME
RUN mkdir -p $HOME && chown -R 1000:0 $HOME 
USER 1000