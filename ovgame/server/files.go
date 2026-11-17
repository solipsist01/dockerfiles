package main

// Bestanden: client (autoupdater) en wereld (streaming).
//
// Client (wat de launcher naar de speler kopieert, /api/client: per regel "crc32 grootte pad"):
//   OVGame.exe, OVLauncher.exe     uit CLIENT_BIN (in de image gebouwd)
//   data/world/world.bin, map.bin  uit GAME_DATA (de data-map van de game, read-only gemount)
//   data/bus/*.txt, data/audio/**
// Stream (tijdens het spelen, /api/world: per regel "crc32 grootte z pad"; z = 1: zlib-gecomprimeerd geleverd):
//   world/tiles/*.bin, raw/luchtfoto/*.jpg, raw/luchtfoto_lo/*.jpg  uit GAME_DATA
// cache/ (in DATA): gecomprimeerde tegels + index, zodat een herstart niet alles opnieuw hoeft te doen.

import (
	"bufio"
	"bytes"
	"compress/zlib"
	"fmt"
	"hash/crc32"
	"io"
	"io/fs"
	"log"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

type fileEntry struct {
	Path  string // relatief, met /
	Abs   string // waar het bestand staat (niet in de index)
	Size  int64
	MTime int64
	CRC   uint32
	Z     bool  // gecomprimeerd geleverd
	ZSize int64 // grootte van het gecomprimeerde bestand
}

type FileStore struct {
	cfg      Config
	mu       sync.RWMutex
	client   map[string]*fileEntry
	world    map[string]*fileEntry
	clientMf []byte
	worldIdx []byte
	worldZ   []byte // index, zelf ook gecomprimeerd (scheelt veel bij duizenden tegels)
	scanning sync.Mutex
	ready    atomic.Bool
	stats    struct {
		worldFiles, clientFiles int
		worldBytes, worldZBytes int64
		clientBytes             int64
		scanned                 time.Time
	}
}

func newFileStore(cfg Config) *FileStore {
	return &FileStore{cfg: cfg, client: map[string]*fileEntry{}, world: map[string]*fileEntry{}}
}

func crcFile(p string) (uint32, error) {
	f, err := os.Open(p)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	h := crc32.NewIEEE()
	if _, err := io.Copy(h, bufio.NewReaderSize(f, 1<<20)); err != nil {
		return 0, err
	}
	return h.Sum32(), nil
}

// welke wereldbestanden comprimeren (JPEG's niet: dat levert niets op)
func compressible(p string) bool {
	e := strings.ToLower(path.Ext(p))
	return e != ".jpg" && e != ".jpeg" && e != ".png"
}

func (s *FileStore) loadIndex(file string) map[string]*fileEntry {
	out := map[string]*fileEntry{}
	f, err := os.Open(file)
	if err != nil {
		return out
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		p := strings.Split(sc.Text(), "\t")
		if len(p) != 6 {
			continue
		}
		size, _ := strconv.ParseInt(p[1], 10, 64)
		mt, _ := strconv.ParseInt(p[2], 10, 64)
		crc, _ := strconv.ParseUint(p[3], 16, 32)
		zs, _ := strconv.ParseInt(p[5], 10, 64)
		out[p[0]] = &fileEntry{Path: p[0], Size: size, MTime: mt, CRC: uint32(crc), Z: p[4] == "1", ZSize: zs}
	}
	return out
}

func saveIndex(file string, m map[string]*fileEntry) {
	var b bytes.Buffer
	for _, e := range sortedEntries(m) {
		z := "0"
		if e.Z {
			z = "1"
		}
		fmt.Fprintf(&b, "%s\t%d\t%d\t%08x\t%s\t%d\n", e.Path, e.Size, e.MTime, e.CRC, z, e.ZSize)
	}
	tmp := file + ".tmp"
	if os.WriteFile(tmp, b.Bytes(), 0o644) == nil {
		os.Rename(tmp, file)
	}
}

func sortedEntries(m map[string]*fileEntry) []*fileEntry {
	l := make([]*fileEntry, 0, len(m))
	for _, e := range m {
		l = append(l, e)
	}
	sort.Slice(l, func(i, j int) bool { return l[i].Path < l[j].Path })
	return l
}

// Een bron: bestanden uit dir verschijnen onder prefix (alleen als keep(naam) waar is; nil = alles)
type source struct {
	prefix, dir string
	keep        func(rel string) bool
}

func ext(exts ...string) func(string) bool {
	return func(rel string) bool {
		e := strings.ToLower(path.Ext(rel))
		for _, x := range exts {
			if e == x {
				return true
			}
		}
		return false
	}
}

func (s *FileStore) clientSources() []source {
	gd := s.cfg.GameData
	return []source{
		// de installer niet: die is alleen voor de eerste download via de website
		{"", s.cfg.ClientBin, func(r string) bool { return ext(".exe", ".dll")(r) && !strings.EqualFold(r, "OVGameSetup.exe") }},
		{"data/world/", filepath.Join(gd, "world"), func(r string) bool { return r == "world.bin" || r == "map.bin" }},
		{"data/bus/", filepath.Join(gd, "bus"), func(r string) bool {
			return strings.HasSuffix(r, ".txt") && !strings.Contains(r, "/") && r != "omroep_nieuw.txt"
		}},
		{"data/audio/", filepath.Join(gd, "audio"), nil},
	}
}

func (s *FileStore) streamSources() []source {
	gd := s.cfg.GameData
	return []source{
		{"world/tiles/", filepath.Join(gd, "world", "tiles"), ext(".bin")},
		{"raw/luchtfoto/", filepath.Join(gd, "raw", "luchtfoto"), ext(".jpg")},
		{"raw/luchtfoto_lo/", filepath.Join(gd, "raw", "luchtfoto_lo"), ext(".jpg")},
	}
}

func walkSources(srcs []source) []found {
	var out []found
	for _, src := range srcs {
		for _, f := range walk(src.dir) {
			if src.keep != nil && !src.keep(f.rel) {
				continue
			}
			f.rel = src.prefix + f.rel
			out = append(out, f)
		}
	}
	return out
}

type found struct {
	rel  string
	abs  string
	size int64
	mt   int64
}

func walk(root string) []found {
	var out []found
	filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		rel = filepath.ToSlash(rel)
		if strings.HasPrefix(path.Base(rel), ".") {
			return nil
		}
		out = append(out, found{rel, p, info.Size(), info.ModTime().UnixNano()})
		return nil
	})
	return out
}

// rescan leest client/ en world/ (opnieuw) in. Ongewijzigde bestanden (zelfde grootte en tijd) komen uit de index.
func (s *FileStore) rescan() error {
	s.scanning.Lock()
	defer s.scanning.Unlock()
	t0 := time.Now()
	cacheDir := filepath.Join(s.cfg.Root, "cache")

	// ---- client ----
	oldC := s.loadIndex(filepath.Join(cacheDir, "client.idx"))
	client := map[string]*fileEntry{}
	var cBytes int64
	for _, f := range walkSources(s.clientSources()) {
		if o, ok := oldC[f.rel]; ok && o.Size == f.size && o.MTime == f.mt {
			o.Abs = f.abs
			client[f.rel] = o
		} else {
			crc, err := crcFile(f.abs)
			if err != nil {
				log.Printf("client/%s: %v", f.rel, err)
				continue
			}
			client[f.rel] = &fileEntry{Path: f.rel, Abs: f.abs, Size: f.size, MTime: f.mt, CRC: crc}
		}
		cBytes += f.size
	}
	saveIndex(filepath.Join(cacheDir, "client.idx"), client)
	var mf bytes.Buffer
	mf.WriteString("OVCLIENT 1\n")
	for _, e := range sortedEntries(client) {
		fmt.Fprintf(&mf, "%08x %d %s\n", e.CRC, e.Size, e.Path)
	}

	// ---- wereld ----
	oldW := s.loadIndex(filepath.Join(cacheDir, "world.idx"))
	files := walkSources(s.streamSources())
	world := make(map[string]*fileEntry, len(files))
	var wmu sync.Mutex
	var done, redo atomic.Int64
	jobs := make(chan found)
	var wg sync.WaitGroup
	workers := s.cfg.Workers
	if workers < 1 {
		workers = 1
	}
	lastLog := time.Now()
	var logMu sync.Mutex
	for i := 0; i < workers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for f := range jobs {
				e, err := s.prepWorld(f, oldW[f.rel], cacheDir, &redo)
				n := done.Add(1)
				if err != nil {
					log.Printf("%s: %v", f.rel, err)
					continue
				}
				e.Abs = f.abs
				wmu.Lock()
				world[f.rel] = e
				wmu.Unlock()
				logMu.Lock()
				if time.Since(lastLog) > 5*time.Second {
					lastLog = time.Now()
					log.Printf("Wereld voorbereiden: %d / %d bestanden (%d gecomprimeerd)", n, len(files), redo.Load())
				}
				logMu.Unlock()
			}
		}()
	}
	for _, f := range files {
		jobs <- f
	}
	close(jobs)
	wg.Wait()
	// verdwenen bestanden uit de cache halen
	for p, o := range oldW {
		if _, ok := world[p]; !ok && o.Z {
			os.Remove(filepath.Join(cacheDir, "z", p+".z"))
		}
	}
	saveIndex(filepath.Join(cacheDir, "world.idx"), world)

	var wi bytes.Buffer
	wi.WriteString("OVWORLD 1\n")
	var wBytes, wzBytes int64
	for _, e := range sortedEntries(world) {
		z := 0
		if e.Z {
			z = 1
			wzBytes += e.ZSize
		} else {
			wzBytes += e.Size
		}
		wBytes += e.Size
		fmt.Fprintf(&wi, "%08x %d %d %s\n", e.CRC, e.Size, z, e.Path)
	}
	var wz bytes.Buffer
	zw, _ := zlib.NewWriterLevel(&wz, zlib.BestCompression)
	zw.Write(wi.Bytes())
	zw.Close()

	s.mu.Lock()
	s.client, s.world = client, world
	s.clientMf, s.worldIdx, s.worldZ = mf.Bytes(), wi.Bytes(), wz.Bytes()
	s.stats.clientFiles, s.stats.clientBytes = len(client), cBytes
	s.stats.worldFiles, s.stats.worldBytes, s.stats.worldZBytes = len(world), wBytes, wzBytes
	s.stats.scanned = time.Now()
	s.mu.Unlock()
	s.ready.Store(true)
	log.Printf("Bestanden klaar in %.1f s: client %d bestanden (%s), wereld %d bestanden (%s, verstuurd %s)",
		time.Since(t0).Seconds(), len(client), human(cBytes), len(world), human(wBytes), human(wzBytes))
	if len(client) == 0 {
		log.Printf("LET OP: geen clientbestanden gevonden (CLIENT_BIN=%s, GAME_DATA=%s)", s.cfg.ClientBin, s.cfg.GameData)
	}
	if len(world) == 0 {
		log.Printf("LET OP: geen tegels/luchtfoto's gevonden in GAME_DATA=%s", s.cfg.GameData)
	}
	return nil
}

func (s *FileStore) prepWorld(f found, old *fileEntry, cacheDir string, redo *atomic.Int64) (*fileEntry, error) {
	z := compressible(f.rel)
	zpath := filepath.Join(cacheDir, "z", f.rel+".z")
	if old != nil && old.Size == f.size && old.MTime == f.mt && old.Z == z {
		if !z {
			c := *old
			return &c, nil
		}
		if st, err := os.Stat(zpath); err == nil && st.Size() == old.ZSize {
			c := *old
			return &c, nil
		}
	}
	e := &fileEntry{Path: f.rel, Size: f.size, MTime: f.mt, Z: z}
	in, err := os.Open(f.abs)
	if err != nil {
		return nil, err
	}
	defer in.Close()
	h := crc32.NewIEEE()
	if !z {
		if _, err := io.Copy(h, bufio.NewReaderSize(in, 1<<20)); err != nil {
			return nil, err
		}
		e.CRC = h.Sum32()
		return e, nil
	}
	redo.Add(1)
	os.MkdirAll(filepath.Dir(zpath), 0o755)
	tmp := zpath + ".tmp"
	out, err := os.Create(tmp)
	if err != nil {
		return nil, err
	}
	bw := bufio.NewWriterSize(out, 1<<20)
	zw, _ := zlib.NewWriterLevel(bw, 6)
	if _, err := io.Copy(io.MultiWriter(zw, h), bufio.NewReaderSize(in, 1<<20)); err != nil {
		out.Close()
		os.Remove(tmp)
		return nil, err
	}
	zw.Close()
	bw.Flush()
	st, _ := out.Stat()
	out.Close()
	if err := os.Rename(tmp, zpath); err != nil {
		return nil, err
	}
	e.CRC = h.Sum32()
	e.ZSize = st.Size()
	return e, nil
}

func human(b int64) string {
	switch {
	case b > 1<<30:
		return fmt.Sprintf("%.2f GB", float64(b)/(1<<30))
	case b > 1<<20:
		return fmt.Sprintf("%.1f MB", float64(b)/(1<<20))
	default:
		return fmt.Sprintf("%.0f kB", float64(b)/(1<<10))
	}
}

func noCache(w http.ResponseWriter) {
	w.Header().Set("Cache-Control", "no-cache, no-store, must-revalidate")
}

// immutable: de URL bevat ?h=<crc>, dus een nieuwe versie heeft een andere URL (Cloudflare mag hem cachen)
func cacheHeaders(w http.ResponseWriter, r *http.Request, crc uint32) {
	if r.URL.Query().Get("h") == fmt.Sprintf("%08x", crc) {
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	} else {
		w.Header().Set("Cache-Control", "public, max-age=60")
	}
	w.Header().Set("ETag", fmt.Sprintf("\"%08x\"", crc))
}

func (s *FileStore) clientManifest(w http.ResponseWriter, r *http.Request) {
	if !s.ready.Load() {
		w.Header().Set("Retry-After", "10")
		http.Error(w, "De server bereidt de bestanden nog voor", http.StatusServiceUnavailable)
		return
	}
	s.mu.RLock()
	b := s.clientMf
	s.mu.RUnlock()
	noCache(w)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Write(b)
}

func (s *FileStore) clientFile(w http.ResponseWriter, r *http.Request) {
	rel := strings.TrimPrefix(r.URL.Path, "/c/")
	s.mu.RLock()
	e := s.client[rel]
	s.mu.RUnlock()
	if e == nil {
		http.NotFound(w, r)
		return
	}
	f, err := os.Open(e.Abs)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	cacheHeaders(w, r, e.CRC)
	w.Header().Set("Content-Type", "application/octet-stream")
	http.ServeContent(w, r, "", time.Time{}, f)
}

func (s *FileStore) worldIndex(w http.ResponseWriter, r *http.Request) {
	if !s.ready.Load() {
		w.Header().Set("Retry-After", "10")
		http.Error(w, "De server bereidt de bestanden nog voor", http.StatusServiceUnavailable)
		return
	}
	s.mu.RLock()
	raw, z := s.worldIdx, s.worldZ
	s.mu.RUnlock()
	noCache(w)
	w.Header().Set("Content-Type", "application/octet-stream")
	if r.URL.Query().Get("z") == "1" {
		w.Write(z)
		return
	}
	w.Write(raw)
}

func (s *FileStore) worldFile(w http.ResponseWriter, r *http.Request) {
	rel := strings.TrimPrefix(r.URL.Path, "/w/")
	s.mu.RLock()
	e := s.world[rel]
	s.mu.RUnlock()
	if e == nil {
		http.NotFound(w, r)
		return
	}
	p := e.Abs
	if e.Z {
		p = filepath.Join(s.cfg.Root, "cache", "z", filepath.FromSlash(rel)+".z")
	}
	f, err := os.Open(p)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	cacheHeaders(w, r, e.CRC)
	w.Header().Set("Content-Type", "application/octet-stream")
	http.ServeContent(w, r, "", time.Time{}, f)
}
