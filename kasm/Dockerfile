FROM kasmweb/desktop:1.10.0-rolling
USER root

ENV HOME /home/kasm-default-profile
ENV STARTUPDIR /dockerstartup
ENV INST_SCRIPTS $STARTUPDIR/install
WORKDIR $HOME

######### Customize Container Here ###########
#https://kasmweb.com/docs/latest/how_to/building_images.html
RUN apt-get update \
    && apt-get install -y apt-transport-https mkvtoolnix mkvtoolnix-gui handbrake puddletag \
               traceroute iputils-ping wget xz-utils curl doublecmd-gtk

RUN wget https://github.com/TheAssassin/AppImageLauncher/releases/download/v2.2.0/appimagelauncher_2.2.0-travis995.0f91801.bionic_amd64.deb \
    && apt-get install ./appimagelauncher_2.2.0-travis995.0f91801.bionic_amd64.deb

RUN cp /usr/share/applications/ghb.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/puddletag.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/org.bunkus.mkvtoolnix-gui.desktop $HOME/Desktop/ \
    && cp /usr/share/applications/doublecmd.desktop $HOME/Desktop/ \
    
    && chmod +x $HOME/Desktop/ghb.desktop \
    && chmod +x $HOME/Desktop/puddletag.desktop \
    && chmod +x $HOME/Desktop/org.bunkus.mkvtoolnix-gui.desktop \
    && chmod +x $HOME/Desktop/doublecmd.desktop \

    && chown 1000:1000 $HOME/Desktop/ghb.desktop \
    && chown 1000:1000 $HOME/Desktop/puddletag.desktop \
    && chown 1000:1000 $HOME/Desktop/org.bunkus.mkvtoolnix-gui.desktop \
    && chown 1000:1000 $HOME/Desktop/doublecmd.desktop

ADD /torbrowser.sh /torbrowser.sh

RUN chmod +x /torbrowser.sh && /torbrowser.sh
#RUN echo "/usr/bin/desktop_ready && /opt/sublime_text/sublime_text &" > $STARTUPDIR/custom_startup.sh \
#&& chmod +x $STARTUPDIR/custom_startup.sh

RUN wget https://cdn.hipwallpaper.com/i/92/9/0Ts6mr.png -O /usr/share/extra/backgrounds/bg_default.png

######### End Customizations ###########

RUN chown 1000:0 $HOME
RUN $STARTUPDIR/set_user_permission.sh $HOME

ENV HOME /home/kasm-user
WORKDIR $HOME
RUN mkdir -p $HOME && chown -R 1000:0 $HOME 
USER 1000