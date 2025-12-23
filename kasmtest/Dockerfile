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
    traceroute iputils-ping wget xz-utils curl dnsutils \
    software-properties-common soundconverter sudo python3-full \
    vlc

RUN wget -U "Mozilla" https://downloads.exodus.com/releases/exodus-linux-x64-25.46.7.deb -O /tmp/exodus.deb && \
    dpkg -i /tmp/exodus.deb || apt-get install -yf && rm /tmp/exodus.deb

RUN pip3 install unidecode

#icons
RUN sed -i 's/%U/www.ofnie.org\/startpage/g' $HOME/Desktop/google-chrome.desktop \
    && echo 'pref("browser.startup.homepage_override.mstone", "ignore");' >> /usr/lib/firefox/browser/defaults/preferences/syspref.js \
    && echo 'pref("browser.startup.homepage", "http://www.ofnie.org/startpage");' >> /usr/lib/firefox/browser/defaults/preferences/syspref.js



######### End Customizations ###########

RUN chown 1000:0 $HOME 
RUN $STARTUPDIR/set_user_permission.sh $HOME

ENV HOME /home/kasm-user
WORKDIR $HOME
RUN mkdir -p $HOME && chown -R 1000:0 $HOME 
USER 1000