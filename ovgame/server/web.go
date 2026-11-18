package main

import (
	"bytes"
	"archive/zip"
	"embed"
	"encoding/json"
	"html/template"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

//go:embed web/index.html
var indexHTML string

// Steam-artwork (tools/make_steam_art.py) en de pagina die het laat zien
//
//go:embed web/steam.html
var steamHTML []byte

//go:embed web/steam/*.png
var steamArt embed.FS

var indexTmpl = template.Must(template.New("index").Parse(indexHTML))

type webUI struct {
	cfg   Config
	hub   *Hub
	files *FileStore
	clk   *Clock
}

// publieke basis-URL: PUBLIC_URL, anders uit de aanvraag (Cloudflare zet X-Forwarded-Proto)
func (u *webUI) baseURL(r *http.Request) string {
	if u.cfg.PublicURL != "" {
		return u.cfg.PublicURL
	}
	scheme := "http"
	if r.TLS != nil || strings.EqualFold(r.Header.Get("X-Forwarded-Proto"), "https") {
		scheme = "https"
	}
	return scheme + "://" + r.Host
}

type statusJSON struct {
	Name        string       `json:"name"`
	Version     string       `json:"version"`
	Protocol    int          `json:"protocol"`
	Players     []PlayerInfo `json:"players"`
	MaxPlayers  int          `json:"maxPlayers"`
	Clock       string       `json:"clock"`
	Password    bool         `json:"password"`
	Ready       bool         `json:"ready"`
	ClientFiles int          `json:"clientFiles"`
	ClientSize  string       `json:"clientSize"`
	WorldFiles  int          `json:"worldFiles"`
	WorldSize   string       `json:"worldSize"`
	WorldSent   string       `json:"worldSent"`
	Launcher    bool         `json:"launcher"`
	Installer   bool         `json:"installer"`
}

func (u *webUI) statusData() statusJSON {
	c, _ := u.clk.now()
	s := statusJSON{
		Name: u.cfg.Name, Version: serverVersion, Protocol: protocolVersion,
		Players: u.hub.playerInfo(), MaxPlayers: u.cfg.MaxPlayers, Password: u.cfg.Password != "",
		Ready: u.files.ready.Load(),
	}
	if s.Players == nil {
		s.Players = []PlayerInfo{}
	}
	s.Clock = time.Date(2000, 1, 1, 0, 0, int(c), 0, time.UTC).Format("15:04")
	u.files.mu.RLock()
	st := u.files.stats
	u.files.mu.RUnlock()
	s.ClientFiles, s.ClientSize = st.clientFiles, human(st.clientBytes)
	s.WorldFiles, s.WorldSize, s.WorldSent = st.worldFiles, human(st.worldBytes), human(st.worldZBytes)
	_, err := os.Stat(u.launcherPath())
	s.Launcher = err == nil
	_, err = os.Stat(filepath.Join(u.cfg.ClientBin, "OVGameSetup.exe"))
	s.Installer = err == nil
	return s
}

func (u *webUI) index(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	noCache(w)
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	data := struct {
		statusJSON
		URL string
	}{u.statusData(), u.baseURL(r)}
	if err := indexTmpl.Execute(w, data); err != nil {
		log.Printf("webpagina: %v", err)
	}
}

func (u *webUI) status(w http.ResponseWriter, r *http.Request) {
	noCache(w)
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(u.statusData())
}

func (u *webUI) launcherPath() string {
	return filepath.Join(u.cfg.ClientBin, "OVLauncher.exe")
}

// De launcher bevat een vaste plek "@@OVSERVERURL@@" + nullen (zie src/launcher/launcher.cpp).
// Bij het downloaden schrijft de server daar zijn eigen adres in, zodat de speler niets hoeft in te stellen.
var urlMarker = []byte("@@OVSERVERURL@@")

const urlSlot = 512

func (u *webUI) launcher(w http.ResponseWriter, r *http.Request) {
	b, err := os.ReadFile(u.launcherPath())
	if err != nil {
		http.Error(w, "De launcher staat (nog) niet op de server.", http.StatusNotFound)
		return
	}
	if i := bytes.Index(b, urlMarker); i >= 0 && i+urlSlot <= len(b) {
		url := u.baseURL(r)
		slot := make([]byte, urlSlot)
		copy(slot, "OVURL="+url)
		copy(b[i:i+urlSlot], slot)
	} else {
		log.Printf("LET OP: launcher zonder URL-plek; spelers moeten het adres zelf invullen")
	}
	noCache(w)
	w.Header().Set("Content-Type", "application/vnd.microsoft.portable-executable")
	w.Header().Set("Content-Disposition", `attachment; filename="OVLauncher.exe"`)
	w.Write(b)
}

// De installer (NSIS, gecomprimeerd) kan niet van binnen worden aangepast. Daarom komt het adres erachter:
// 512 bytes "OVURL=https://...\r\n" + nullen. NSIS negeert data na het einde van het installatiepakket;
// de installer leest de laatste 512 bytes van zichzelf (src/installer/ovgame.nsi, .onInit).
func (u *webUI) installer(w http.ResponseWriter, r *http.Request) {
	b, err := os.ReadFile(filepath.Join(u.cfg.ClientBin, "OVGameSetup.exe"))
	if err != nil {
		http.Error(w, "De installer staat (nog) niet op de server.", http.StatusNotFound)
		return
	}
	tail := make([]byte, urlSlot)
	copy(tail, "OVURL="+u.baseURL(r)+"\r\n")
	noCache(w)
	w.Header().Set("Content-Type", "application/vnd.microsoft.portable-executable")
	w.Header().Set("Content-Disposition", `attachment; filename="OVGameSetup.exe"`)
	w.Write(b)
	w.Write(tail)
}

// /steam: pagina met de artwork; /steam/<naam>.png de plaatjes; /steam/ovgame-steam-art.zip alles in een zip
func (u *webUI) steam(w http.ResponseWriter, r *http.Request) {
	name := strings.TrimPrefix(r.URL.Path, "/steam")
	name = strings.TrimPrefix(name, "/")
	switch {
	case name == "":
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Cache-Control", "no-cache")
		w.Write(steamHTML)
	case name == "ovgame-steam-art.zip":
		w.Header().Set("Content-Type", "application/zip")
		w.Header().Set("Content-Disposition", `attachment; filename="ovgame-steam-art.zip"`)
		zw := zip.NewWriter(w)
		entries, _ := steamArt.ReadDir("web/steam")
		for _, e := range entries {
			data, err := steamArt.ReadFile("web/steam/" + e.Name())
			if err != nil {
				continue
			}
			f, err := zw.CreateHeader(&zip.FileHeader{Name: "ovgame-" + e.Name(), Method: zip.Store, Modified: time.Now()})
			if err == nil {
				f.Write(data)
			}
		}
		zw.Close()
	case strings.HasSuffix(name, ".png") && !strings.Contains(name, "/"):
		data, err := steamArt.ReadFile("web/steam/" + name)
		if err != nil {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "image/png")
		w.Header().Set("Cache-Control", "public, max-age=3600")
		w.Write(data)
	default:
		http.NotFound(w, r)
	}
}
