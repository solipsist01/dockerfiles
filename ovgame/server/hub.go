package main

// Spelers via WebSocket. Berichten zijn tekst, velden gescheiden door tabs (makkelijk in C++ te lezen).
//
// client -> server
//   hello <protocol> <wachtwoord> <uiterlijk>     eerste bericht; uiterlijk = Appearance::serialize() (bevat de naam)
//   st <velden...>                                 eigen toestand (positie, voertuig, animatie), ~10x per seconde
//   look <uiterlijk>                               ander personage gekozen
//   chat <tekst>
//   ping <ms>
// server -> client
//   welcome <id> <klok> <weekdag> <servernaam> <motd>
//   deny <reden>
//   join <id> <uiterlijk>          (ook voor iedereen die er al is, direct na welcome)
//   look <id> <uiterlijk>
//   leave <id>
//   st <id> <velden...>            doorgestuurd zoals ontvangen
//   chat <id> <naam> <tekst>       id 0 = server
//   clock <klok> <weekdag>         elke 5 s; klok = seconden na middernacht (spelklok), weekdag 0 = zondag
//   pong <ms> <klok>

import (
	"fmt"
	"log"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
	_ "time/tzdata"

	"github.com/gorilla/websocket"
)

const protocolVersion = 1

// ---------- klok ----------
type Clock struct {
	loc    *time.Location
	fixed  bool
	start  time.Time
	offset float64 // seconden na middernacht bij start
}

func newClock(mode string) *Clock {
	loc, err := time.LoadLocation("Europe/Amsterdam")
	if err != nil {
		loc = time.UTC
	}
	c := &Clock{loc: loc, start: time.Now()}
	var h, m int
	if _, err := fmt.Sscanf(mode, "%d:%d", &h, &m); err == nil && mode != "real" {
		c.fixed = true
		c.offset = float64(h*3600 + m*60)
	}
	return c
}

// spelklok (s na middernacht) en weekdag
func (c *Clock) now() (float64, int) {
	t := time.Now().In(c.loc)
	if c.fixed {
		s := c.offset + time.Since(c.start).Seconds()
		day := int(s / 86400)
		return s - float64(day)*86400, (int(c.start.In(c.loc).Weekday()) + day) % 7
	}
	mid := time.Date(t.Year(), t.Month(), t.Day(), 0, 0, 0, 0, c.loc)
	return t.Sub(mid).Seconds(), int(t.Weekday())
}

// ---------- spelers ----------
type Player struct {
	id      int
	name    string
	look    string
	conn    *websocket.Conn
	send    chan []byte
	ip      string
	joined  time.Time
	lastSt  []byte
	lastMsg time.Time
	chatT   []time.Time
}

type Hub struct {
	cfg     Config
	clk     *Clock
	mu      sync.RWMutex
	players map[int]*Player
	nextID  int
}

var upgrader = websocket.Upgrader{
	ReadBufferSize:  4096,
	WriteBufferSize: 16384,
	CheckOrigin:     func(r *http.Request) bool { return true },
}

func newHub(cfg Config, clk *Clock) *Hub {
	return &Hub{cfg: cfg, clk: clk, players: map[int]*Player{}, nextID: 1}
}

func clean(s string, max int) string {
	s = strings.Map(func(r rune) rune {
		if r == '\t' || r == '\n' || r == '\r' || r < 32 {
			return ' '
		}
		return r
	}, s)
	if len(s) > max {
		s = s[:max]
	}
	return strings.TrimSpace(s)
}

func nameOf(look string) string {
	for _, kv := range strings.Split(look, ";") {
		k, v, ok := strings.Cut(kv, "=")
		if ok && strings.TrimSpace(k) == "naam" {
			if n := clean(v, 24); n != "" {
				return n
			}
		}
	}
	return "Reiziger"
}

func msg(parts ...string) []byte { return []byte(strings.Join(parts, "\t")) }

func (h *Hub) run() {
	t := time.NewTicker(5 * time.Second)
	for range t.C {
		c, wd := h.clk.now()
		h.broadcast(msg("clock", strconv.FormatFloat(c, 'f', 3, 64), strconv.Itoa(wd)), 0)
	}
}

func (h *Hub) broadcast(b []byte, except int) {
	h.mu.RLock()
	defer h.mu.RUnlock()
	for id, p := range h.players {
		if id == except {
			continue
		}
		select {
		case p.send <- b:
		default: // te traag: dit bericht overslaan
		}
	}
}

func (h *Hub) broadcastSystem(text string) {
	if text = clean(text, 300); text != "" {
		h.broadcast(msg("chat", "0", "Server", text), 0)
	}
}

func (h *Hub) playerNames() []string {
	h.mu.RLock()
	defer h.mu.RUnlock()
	var n []string
	for _, p := range h.players {
		n = append(n, p.name)
	}
	sort.Strings(n)
	return n
}

type PlayerInfo struct {
	Name    string `json:"name"`
	Minutes int    `json:"minutes"`
}

func (h *Hub) playerInfo() []PlayerInfo {
	h.mu.RLock()
	defer h.mu.RUnlock()
	var out []PlayerInfo
	for _, p := range h.players {
		out = append(out, PlayerInfo{p.name, int(time.Since(p.joined).Minutes())})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

func (h *Hub) kick(name string) bool {
	h.mu.RLock()
	var target *Player
	for _, p := range h.players {
		if strings.EqualFold(p.name, name) {
			target = p
		}
	}
	h.mu.RUnlock()
	if target == nil {
		return false
	}
	target.conn.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(4001, "verwijderd door de beheerder"), time.Now().Add(time.Second))
	target.conn.Close()
	return true
}

func (h *Hub) serveWS(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	conn.SetReadLimit(8192)
	ip := clientIP(r)

	// eerst hello
	conn.SetReadDeadline(time.Now().Add(15 * time.Second))
	_, data, err := conn.ReadMessage()
	if err != nil {
		conn.Close()
		return
	}
	f := strings.Split(string(data), "\t")
	deny := func(reason string) {
		conn.WriteMessage(websocket.TextMessage, msg("deny", reason))
		conn.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.ClosePolicyViolation, ""), time.Now().Add(time.Second))
		conn.Close()
		log.Printf("%s geweigerd: %s", ip, reason)
	}
	if len(f) < 4 || f[0] != "hello" {
		deny("Onbekend protocol")
		return
	}
	if v, _ := strconv.Atoi(f[1]); v != protocolVersion {
		deny(fmt.Sprintf("Verkeerde versie van de game (protocol %s, server %d). Start de game via de launcher om bij te werken.", f[1], protocolVersion))
		return
	}
	if h.cfg.Password != "" && f[2] != h.cfg.Password {
		deny("Verkeerd wachtwoord")
		return
	}
	look := clean(f[3], 1500)
	p := &Player{name: nameOf(look), look: look, conn: conn, send: make(chan []byte, 256), ip: ip, joined: time.Now()}

	h.mu.Lock()
	if len(h.players) >= h.cfg.MaxPlayers {
		h.mu.Unlock()
		deny("De server is vol")
		return
	}
	// dubbele namen: nummer erachter
	base, n := p.name, 2
	for taken := true; taken; {
		taken = false
		for _, o := range h.players {
			if strings.EqualFold(o.name, p.name) {
				taken = true
				p.name = fmt.Sprintf("%s %d", base, n)
				n++
				break
			}
		}
	}
	p.id = h.nextID
	h.nextID++
	type known struct {
		id     int
		look   string
		lastSt []byte
	}
	others := make([]known, 0, len(h.players))
	for _, o := range h.players {
		others = append(others, known{o.id, o.look, o.lastSt})
	}
	h.players[p.id] = p
	h.mu.Unlock()

	go p.writer()
	c, wd := h.clk.now()
	p.send <- msg("welcome", strconv.Itoa(p.id), strconv.FormatFloat(c, 'f', 3, 64), strconv.Itoa(wd), clean(h.cfg.Name, 60), clean(h.cfg.Motd, 300))
	for _, o := range others {
		p.send <- msg("join", strconv.Itoa(o.id), o.look)
		if o.lastSt != nil {
			p.send <- o.lastSt
		}
	}
	h.broadcast(msg("join", strconv.Itoa(p.id), p.look), p.id)
	h.broadcastSystem(p.name + " doet mee")
	log.Printf("%s (%s) doet mee, id %d, %d speler(s)", p.name, ip, p.id, len(others)+1)

	h.reader(p)

	h.mu.Lock()
	delete(h.players, p.id)
	left := len(h.players)
	h.mu.Unlock()
	close(p.send)
	h.broadcast(msg("leave", strconv.Itoa(p.id)), 0)
	h.broadcastSystem(p.name + " is weg")
	log.Printf("%s is weg (na %d min), %d speler(s)", p.name, int(time.Since(p.joined).Minutes()), left)
}

func (p *Player) writer() {
	ping := time.NewTicker(20 * time.Second)
	defer ping.Stop()
	for {
		select {
		case b, ok := <-p.send:
			if !ok {
				p.conn.Close()
				return
			}
			p.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := p.conn.WriteMessage(websocket.TextMessage, b); err != nil {
				p.conn.Close()
				return
			}
		case <-ping.C:
			p.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := p.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				p.conn.Close()
				return
			}
		}
	}
}

func (h *Hub) reader(p *Player) {
	idS := strconv.Itoa(p.id)
	for {
		p.conn.SetReadDeadline(time.Now().Add(60 * time.Second))
		_, data, err := p.conn.ReadMessage()
		if err != nil {
			return
		}
		f := strings.Split(string(data), "\t")
		switch f[0] {
		case "st":
			// niet vaker dan ~25x per seconde doorgeven
			if time.Since(p.lastMsg) < 40*time.Millisecond || len(data) > 600 {
				continue
			}
			p.lastMsg = time.Now()
			out := msg(append([]string{"st", idS}, f[1:]...)...)
			h.mu.Lock()
			p.lastSt = out
			h.mu.Unlock()
			h.broadcast(out, p.id)
		case "look":
			if len(f) < 2 {
				continue
			}
			look := clean(f[1], 1500)
			h.mu.Lock()
			p.look = look // naam blijft dezelfde in deze sessie
			h.mu.Unlock()
			h.broadcast(msg("look", idS, look), p.id)
		case "chat":
			if len(f) < 2 {
				continue
			}
			text := clean(strings.Join(f[1:], " "), 200)
			if text == "" {
				continue
			}
			// max 5 berichten per 10 s
			now := time.Now()
			k := 0
			for _, t := range p.chatT {
				if now.Sub(t) < 10*time.Second {
					p.chatT[k] = t
					k++
				}
			}
			p.chatT = p.chatT[:k]
			if len(p.chatT) >= 5 {
				p.send <- msg("chat", "0", "Server", "Niet zo snel...")
				continue
			}
			p.chatT = append(p.chatT, now)
			log.Printf("<%s> %s", p.name, text)
			h.broadcast(msg("chat", idS, p.name, text), 0)
		case "ping":
			if len(f) < 2 {
				continue
			}
			c, _ := h.clk.now()
			select {
			case p.send <- msg("pong", clean(f[1], 20), strconv.FormatFloat(c, 'f', 3, 64)):
			default:
			}
		}
	}
}
