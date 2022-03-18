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
    && apt-get update \
    && apt-get install -y apt-transport-https mkvtoolnix mkvtoolnix-gui handbrake puddletag \
               traceroute iputils-ping wget xz-utils curl \
    && echo "deb https://download.sublimetext.com/ apt/stable/" |  tee /etc/apt/sources.list.d/sublime-text.list \
    && apt-get update \
    && apt-get install sublime-text \
    && cp /usr/share/applications/sublime_text.desktop $HOME/Desktop/ \
    && chmod +x $HOME/Desktop/sublime_text.desktop \
    && chown 1000:1000 $HOME/Desktop/sublime_text.desktop

RUN set -xe \
    && apt-get install -y xz-utils curl \
    && TOR_HOME=$HOME/tor-browser/ \
    && mkdir -p $TOR_HOME \
    && TOR_URL=$(curl -q https://www.torproject.org/download/ | grep downloadLink | grep linux64 | sed 's/.*href="//g'  | cut -d '"' -f1 | head -1) \
    && wget --quiet https://www.torproject.org/${TOR_URL} -O /tmp/torbrowser.tar.xz \
    && tar -xJf /tmp/torbrowser.tar.xz -C $TOR_HOME \
    && rm /tmp/torbrowser.tar.xz \
    && cp $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop.bak \
    && cp $TOR_HOME/tor-browser_en-US/Browser/browser/chrome/icons/default/default128.png /usr/share/icons/tor.png \
    && chown 1000:0 /usr/share/icons/tor.png \
    && sed -i 's/^Name=.*/Name=Tor Browser/g' $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop \
    && sed -i 's/Icon=.*/Icon=\/usr\/share\/icons\/tor.png/g' $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop \
    && sed -i 's/Exec=.*/Exec=sh -c \x27"$HOME\/tor-browser\/tor-browser_en-US\/Browser\/start-tor-browser" --detach || ([ !  -x "$HOME\/tor-browser\/tor-browser_en-US\/Browser\/start-tor-browser" ] \&\& "$(dirname "$*")"\/Browser\/start-tor-browser --detach)\x27 dummy %k/g'  $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop \
    && cat >> $TOR_HOME/tor-browser_en-US/Browser/TorBrowser/Data/Browser/profile.default/prefs.js <<EOL \
    && user_pref("extensions.torlauncher.prompt_at_startup", false); \
    && user_pref("extensions.torlauncher.quickstart", true); \
    && user_pref("browser.download.lastDir", "/home/kasm-user/Downloads"); \
EOL

# Maintain backwards compatability with old image definitions that expect tor to be launched from /tmp
mkdir -p /tmp/tor-browser_en-US/Browser/
ln -s $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop /tmp/tor-browser_en-US/Browser/start-tor-browser.desktop 

chown -R 1000:0 $TOR_HOME/


cp $TOR_HOME/tor-browser_en-US/start-tor-browser.desktop $HOME/Desktop/
chown 1000:0  $HOME/Desktop/start-tor-browser.desktop

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