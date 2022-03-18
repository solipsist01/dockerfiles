FROM kasmweb/desktop:1.10.0-rolling
USER root

ENV HOME /home/kasm-default-profile
ENV STARTUPDIR /dockerstartup
ENV INST_SCRIPTS $STARTUPDIR/install
WORKDIR $HOME

######### Customize Container Here ###########
#https://kasmweb.com/docs/latest/how_to/building_images.html
RUN  wget -qO - https://download.sublimetext.com/sublimehq-pub.gpg | apt-key add - \
    && add-apt-repository ppa:ubuntuhandbook1/apps -y \
    && add-apt-repository ppa:micahflee/ppa -y \
    && apt-get update \
    && apt-get install -y apt-transport-https mkvtoolnix mkvtoolnix-gui handbrake puddletag \
               torbrowser-launcher traceroute iputils-ping wget \
    && echo "deb https://download.sublimetext.com/ apt/stable/" |  tee /etc/apt/sources.list.d/sublime-text.list \
    && apt-get update \
    && apt-get install sublime-text \
    && cp /usr/share/applications/sublime_text.desktop $HOME/Desktop/ \
    && chmod +x $HOME/Desktop/sublime_text.desktop \
    && chown 1000:1000 $HOME/Desktop/sublime_text.desktop

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