// OV Game server
//
// Eén programma dat alles doet wat een multiplayer-server nodig heeft:
//   - webinterface (status, spelers, download van de launcher)
//   - client-bestanden voor de autoupdater (manifest + bestanden uit de map client/)
//   - wereld-streaming: tegels en luchtfoto's uit de map world/, eenmalig gecomprimeerd in cache/
//   - spelers: WebSocket /ws (posities, chat, gedeelde klok)
//
// De server praat gewoon HTTP; HTTPS komt van Cloudflare (tunnel) ervoor.
// Instellingen via omgevingsvariabelen (docker compose) of vlaggen, zie config().
package main

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

const serverVersion = "1.0.0"

type Config struct {
	Port       int
	Root       string // eigen schrijfbare map (cache)
	GameData   string // data-map van de game (read-only): world/, raw/, bus/, audio/
	ClientBin  string // OVGame.exe en OVLauncher.exe
	Name       string
	Motd       string
	Password   string
	MaxPlayers int
	PublicURL  string // bijv. https://ovgame.ofnie.org (leeg = afleiden uit de aanvraag)
	Clock      string // "real" (Nederlandse tijd) of "UU:MM" (begintijd, loopt daarna gewoon door)
	Workers    int
}

func env(k, def string) string {
	if v, ok := os.LookupEnv(k); ok && v != "" {
		return v
	}
	return def
}

func envInt(k string, def int) int {
	if v, err := strconv.Atoi(env(k, "")); err == nil {
		return v
	}
	return def
}

func config() Config {
	var c Config
	flag.IntVar(&c.Port, "port", envInt("PORT", 8080), "HTTP-poort")
	flag.StringVar(&c.Root, "root", env("DATA", "/data"), "schrijfbare map (cache)")
	flag.StringVar(&c.GameData, "gamedata", env("GAME_DATA", "/gamedata"), "data-map van de game")
	flag.StringVar(&c.ClientBin, "clientbin", env("CLIENT_BIN", "/opt/ovgame/client"), "map met OVGame.exe en OVLauncher.exe")
	flag.StringVar(&c.Name, "name", env("SERVER_NAME", "OV Game server"), "servernaam")
	flag.StringVar(&c.Motd, "motd", env("MOTD", "Welkom! Druk op T om te chatten."), "welkomstbericht")
	flag.StringVar(&c.Password, "password", env("SERVER_PASSWORD", ""), "wachtwoord (leeg = geen)")
	flag.IntVar(&c.MaxPlayers, "max", envInt("MAX_PLAYERS", 32), "maximaal aantal spelers")
	flag.StringVar(&c.PublicURL, "url", env("PUBLIC_URL", ""), "publieke URL (https://...)")
	flag.StringVar(&c.Clock, "clock", env("GAME_CLOCK", "real"), "klok: real of UU:MM")
	flag.IntVar(&c.Workers, "workers", envInt("PREP_WORKERS", 4), "threads voor het comprimeren van de wereld")
	flag.Parse()
	c.PublicURL = strings.TrimRight(c.PublicURL, "/")
	for _, p := range []*string{&c.Root, &c.GameData, &c.ClientBin} {
		if abs, err := filepath.Abs(*p); err == nil {
			*p = abs
		}
	}
	return c
}

func main() {
	log.SetFlags(log.Ldate | log.Ltime)
	cfg := config()
	log.Printf("OV Game server %s: data %s, gamedata %s, client %s", serverVersion, cfg.Root, cfg.GameData, cfg.ClientBin)
	os.MkdirAll(filepath.Join(cfg.Root, "cache"), 0o755)

	clk := newClock(cfg.Clock)
	files := newFileStore(cfg)
	hub := newHub(cfg, clk)
	go hub.run()

	mux := http.NewServeMux()
	web := &webUI{cfg: cfg, hub: hub, files: files, clk: clk}
	mux.HandleFunc("/", web.index)
	mux.HandleFunc("/api/status", web.status)
	mux.HandleFunc("/steam", web.steam)
	mux.HandleFunc("/steam/", web.steam)
	mux.HandleFunc("/download/OVLauncher.exe", web.launcher)
	mux.HandleFunc("/download/OVGameSetup.exe", web.installer)
	mux.HandleFunc("/api/client", files.clientManifest)
	mux.HandleFunc("/c/", files.clientFile)
	mux.HandleFunc("/api/world", files.worldIndex)
	mux.HandleFunc("/w/", files.worldFile)
	mux.HandleFunc("/ws", hub.serveWS)

	srv := &http.Server{
		Addr:              fmt.Sprintf("0.0.0.0:%d", cfg.Port),
		Handler:           logRequests(mux),
		ReadHeaderTimeout: 20 * time.Second,
	}
	go func() {
		log.Printf("Server gestart op poort %d (%s)", cfg.Port, cfg.Name)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("HTTP: %v", err)
		}
	}()

	// bestanden inlezen (eerste keer: de wereld comprimeren, kan een paar minuten duren)
	go func() {
		if err := files.rescan(); err != nil {
			log.Printf("FOUT bij het inlezen van de bestanden: %v", err)
		}
	}()

	// console (docker attach / stdin_open in compose)
	quit := make(chan struct{})
	go console(hub, files, quit)
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	// SIGHUP (docker kill -s HUP ovgame): bestanden opnieuw inlezen, bijv. na convert of een nieuwe build
	hup := make(chan os.Signal, 1)
	signal.Notify(hup, syscall.SIGHUP)
	go func() {
		for range hup {
			log.Printf("SIGHUP: bestanden opnieuw inlezen")
			files.rescan()
		}
	}()
	select {
	case <-sig:
	case <-quit:
	}
	log.Printf("Server stopt...")
	hub.broadcastSystem("De server wordt gestopt.")
	time.Sleep(300 * time.Millisecond)
	srv.Close()
	log.Printf("Gestopt.")
}

func console(hub *Hub, files *FileStore, quit chan struct{}) {
	sc := bufio.NewScanner(os.Stdin)
	help := "commando's: list, say <tekst>, kick <naam>, reload, time, stop"
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		cmd, arg, _ := strings.Cut(line, " ")
		switch strings.ToLower(cmd) {
		case "":
		case "help", "?":
			log.Print(help)
		case "list":
			names := hub.playerNames()
			log.Printf("%d speler(s) online: %s", len(names), strings.Join(names, ", "))
		case "say":
			hub.broadcastSystem(arg)
			log.Printf("[server] %s", arg)
		case "kick":
			if hub.kick(arg) {
				log.Printf("%s is verwijderd", arg)
			} else {
				log.Printf("geen speler '%s'", arg)
			}
		case "reload":
			if err := files.rescan(); err != nil {
				log.Printf("FOUT: %v", err)
			}
		case "time":
			c, wd := hub.clk.now()
			log.Printf("spelklok %02d:%02d:%02d, dag %d", int(c)/3600, int(c)/60%60, int(c)%60, wd)
		case "stop", "exit", "quit":
			close(quit)
			return
		default:
			log.Printf("onbekend commando '%s' - %s", cmd, help)
		}
	}
}

type statusWriter struct {
	http.ResponseWriter
	code int
}

func (w *statusWriter) WriteHeader(c int) { w.code = c; w.ResponseWriter.WriteHeader(c) }

// Hijack doorgeven (nodig voor WebSockets)
func (w *statusWriter) Unwrap() http.ResponseWriter { return w.ResponseWriter }

func logRequests(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// alleen de interessante aanvragen loggen (niet elke tegel)
		if strings.HasPrefix(r.URL.Path, "/download/") || r.URL.Path == "/api/client" {
			log.Printf("%s %s %s", clientIP(r), r.Method, r.URL.Path)
		}
		h.ServeHTTP(w, r)
	})
}

// clientIP: het echte adres van de speler. Achter een proxy (Traefik, Cloudflare-tunnel, nginx) staat dat in een header;
// die headers worden alleen geloofd als de aanvraag van een proxy op het eigen netwerk komt (privé- of loopbackadres),
// anders zou iedereen zich met een zelfgemaakte header een ander adres kunnen geven.
// Volgorde: CF-Connecting-IP / True-Client-IP (Cloudflare, ook als Traefik achter Cloudflare zit), X-Real-Ip (Traefik,
// nginx), Forwarded (RFC 7239: for=...), X-Forwarded-For (eerste adres), en anders het adres van de verbinding.
func clientIP(r *http.Request) string {
	peer := hostOnly(r.RemoteAddr)
	if !trustedProxy(peer) {
		return peer
	}
	for _, h := range []string{"CF-Connecting-IP", "True-Client-IP", "X-Real-Ip"} {
		if ip := validIP(r.Header.Get(h)); ip != "" {
			return ip
		}
	}
	if f := r.Header.Get("Forwarded"); f != "" {
		first, _, _ := strings.Cut(f, ",")
		for _, part := range strings.Split(first, ";") {
			k, v, ok := strings.Cut(strings.TrimSpace(part), "=")
			if ok && strings.EqualFold(k, "for") {
				if ip := validIP(strings.Trim(v, `"`)); ip != "" {
					return ip
				}
			}
		}
	}
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		first, _, _ := strings.Cut(xff, ",")
		if ip := validIP(first); ip != "" {
			return ip
		}
	}
	return peer
}

// hostOnly: "1.2.3.4:5678" / "[::1]:80" -> adres zonder poort
func hostOnly(addr string) string {
	if h, _, err := net.SplitHostPort(addr); err == nil {
		return h
	}
	return strings.Trim(addr, "[]")
}

// validIP: een geldig IP-adres uit een headerwaarde (ook "[2001:db8::1]:443" of "1.2.3.4:80"), anders ""
func validIP(v string) string {
	v = strings.TrimSpace(v)
	if v == "" {
		return ""
	}
	if ip := net.ParseIP(v); ip != nil {
		return ip.String()
	}
	if ip := net.ParseIP(hostOnly(v)); ip != nil {
		return ip.String()
	}
	return ""
}

// trustedProxy: verbinding vanaf het eigen netwerk (Docker-netwerk, LAN, localhost)
func trustedProxy(addr string) bool {
	ip := net.ParseIP(addr)
	return ip != nil && (ip.IsLoopback() || ip.IsPrivate() || ip.IsLinkLocalUnicast())
}
