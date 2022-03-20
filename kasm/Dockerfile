FROM kasmweb/desktop:1.10.0-rolling
USER root

ENV HOME /home/kasm-default-profile
ENV STARTUPDIR /dockerstartup
ENV INST_SCRIPTS $STARTUPDIR/install
WORKDIR $HOME

######### Customize Container Here ###########
#https://kasmweb.com/docs/latest/how_to/building_images.html
RUN add-apt-repository -y ppa:pedrocastro/ppa \
    && apt-add-repository -y ppa:remmina-ppa-team/remmina-next \
    && add-apt-repository -r -y ppa:libreoffice/ppa \

    && apt-get update \
    && apt-get install -y apt-transport-https mkvtoolnix mkvtoolnix-gui handbrake puddletag \
               traceroute iputils-ping wget xz-utils curl doublecmd-gtk vlc dnsutils gnome-subtitles \
               putty remmina remmina-plugin-rdp remmina-plugin-secret software-properties-common libreoffice \
               #Teamviewer dependencies
               libminizip1

RUN TEMP_DEB="$(mktemp)" && wget -O "$TEMP_DEB" 'https://download.teamviewer.com/download/linux/teamviewer_amd64.deb' && dpkg -i "$TEMP_DEB" && rm -f "$TEMP_DEB"

RUN mkdir /appimages && mkdir /appimages/losslesscut \
    && wget https://github.com/mifi/lossless-cut/releases/latest/download/LosslessCut-linux-x86_64.AppImage -O /appimages/losslesscut/LosslessCut-linux-x86_64.AppImage \
    && cd /appimages/losslesscut && chmod +x /appimages/losslesscut/LosslessCut-linux-x86_64.AppImage && ./LosslessCut-linux-x86_64.AppImage --appimage-extract \
    && chmod +x /appimages/losslesscut/squashfs-root/losslesscut && rm /appimages/losslesscut/LosslessCut-linux-x86_64.AppImage \
    && DESKTOPITEM="$HOME/Desktop/losslesscut.desktop" \
    && echo "[Desktop Entry]" > $DESKTOPITEM \
    && echo "Name=LosslessCut" >> $DESKTOPITEM \
    && echo "Exec=/appimages/losslesscut/squashfs-root/losslesscut --no-sandbox" >> $DESKTOPITEM \
    && echo "Terminal=false" >> $DESKTOPITEM \
    && echo "Type=Application" >> $DESKTOPITEM \
    && echo "Icon=/appimages/losslesscut/squashfs-root/losslesscut.png" >> $DESKTOPITEM \
    && echo "StartupWMClass=LosslessCut" >> $DESKTOPITEM \
    && echo "X-AppImage-Version=3.44.0" >> $DESKTOPITEM \
    && echo "Categories=Utility;" >> $DESKTOPITEM \
    && chmod +x $DESKTOPITEM && chown 1000:0 $DESKTOPITEM \
    && chown 1000:0 /appimages -R

RUN cp /usr/share/applications/ghb.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/puddletag.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/org.bunkus.mkvtoolnix-gui.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/doublecmd.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/org.gnome.GnomeSubtitles.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/vlc.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/org.remmina.Remmina.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/exo-terminal-emulator.desktop $HOME/Desktop/ \
    
    && chmod +x $HOME/Desktop/ghb.desktop \
    && chmod +x $HOME/Desktop/puddletag.desktop \
    && chmod +x $HOME/Desktop/org.bunkus.mkvtoolnix-gui.desktop \
    && chmod +x $HOME/Desktop/doublecmd.desktop \
    && chmod +x $HOME/Desktop/org.gnome.GnomeSubtitles.desktop \
    && chmod +x $HOME/Desktop/vlc.desktop \
    && chmod +x $HOME/Desktop/org.remmina.Remmina.desktop \
    && chmod +x $HOME/Desktop/exo-terminal-emulator.desktop \

    && chown 1000:1000 $HOME/Desktop/ghb.desktop \
    && chown 1000:1000 $HOME/Desktop/puddletag.desktop \
    && chown 1000:1000 $HOME/Desktop/org.bunkus.mkvtoolnix-gui.desktop \
    && chown 1000:1000 $HOME/Desktop/doublecmd.desktop \
    && chown 1000:1000 $HOME/Desktop/org.gnome.GnomeSubtitles.desktop \
    && chown 1000:1000 $HOME/Desktop/vlc.desktop \
    && chown 1000:1000 $HOME/Desktop/org.remmina.Remmina.desktop \
    && chown 1000:1000 $HOME/Desktop/exo-terminal-emulator.desktop \

    && sed -i 's/%U/www.wenro.nl\/startpage/g' $HOME/Desktop/google-chrome.desktop \
    && echo 'pref("browser.startup.homepage_override.mstone", "ignore");' >> /usr/lib/firefox/browser/defaults/preferences/syspref.js \
    && echo 'pref("browser.startup.homepage", "http://www.wenro.nl/startpage");' >> /usr/lib/firefox/browser/defaults/preferences/syspref.js



ADD /torbrowser.sh /torbrowser.sh

RUN chmod +x /torbrowser.sh && /torbrowser.sh
#RUN echo "/usr/bin/desktop_ready && /opt/sublime_text/sublime_text &" > $STARTUPDIR/custom_startup.sh \
#&& chmod +x $STARTUPDIR/custom_startup.sh

RUN wget https://cdn.hipwallpaper.com/i/92/9/0Ts6mr.png -O /usr/share/extra/backgrounds/bg_default.png

######### End Customizations ###########

RUN chown 1000:0 $HOME
RUN chown 1000:0 /opt/teamviewer -R
RUN $STARTUPDIR/set_user_permission.sh $HOME

ENV HOME /home/kasm-user
WORKDIR $HOME
RUN mkdir -p $HOME && chown -R 1000:0 $HOME 
USER 1000