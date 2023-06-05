FROM kasmweb/ubuntu-jammy-desktop:1.13.0-rolling
USER root

ENV HOME /home/kasm-default-profile
ENV STARTUPDIR /dockerstartup
ENV INST_SCRIPTS $STARTUPDIR/install
WORKDIR $HOME

######### Customize Container Here ###########
#https://kasmweb.com/docs/latest/how_to/building_images.html
RUN add-apt-repository -y ppa:kdenlive/kdenlive-stable \
    &&apt-get update  && apt-get install -y flatpak apt-transport-https mc \
    traceroute iputils-ping wget xz-utils curl doublecmd-gtk dnsutils \
    software-properties-common  soundconverter python3-pip python3-pyparsing python3-mutagen \
    python3-pyqt5 python3-configobj python3-pyqt5.qtsvg libapr1 libaprutil1 libxcb-cursor0 sudo \
    vlc kdenlive intel-gpu-tools

RUN wget https://raw.githubusercontent.com/solipsist01/dockerfiles/master/kasm/bg_default.png -O /usr/share/extra/backgrounds/bg_default.png \
    && echo 'kasm-user ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers

######### End Customizations ###########

RUN chown 1000:0 $HOME 
RUN $STARTUPDIR/set_user_permission.sh $HOME

ENV HOME /home/kasm-user
WORKDIR $HOME
RUN mkdir -p $HOME && chown -R 1000:0 $HOME 
USER 1000