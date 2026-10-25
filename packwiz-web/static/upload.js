// Uploads the picked jars one request per file (big files in chunks), a few
// at a time, so no request comes near Cloudflare's size limit or timeout.
// Ends with one "finish" request that refreshes the packs, then reloads.
(function () {
  var form = document.getElementById("upload-form");
  if (!form || !window.fetch) return;

  var PARALLEL = 3;
  var RETRIES = 3;
  var chunkSize = parseInt(form.dataset.chunkMb, 10) * 1024 * 1024;
  var box = document.getElementById("upload-progress");
  var bar = box.querySelector(".bar");
  var status = box.querySelector(".status");
  var log = box.querySelector(".upload-log");
  var busy = false;

  window.addEventListener("beforeunload", function (e) {
    if (busy) { e.preventDefault(); e.returnValue = ""; }
  });

  function baseFields() {
    var fd = new FormData();
    fd.append("csrf", form.elements.csrf.value);
    fd.append("side", form.elements.side.value);
    form.querySelectorAll("input[name=also]:checked").forEach(function (cb) {
      fd.append("also", cb.value);
    });
    return fd;
  }

  function uploadId() {
    var bytes = new Uint8Array(16);
    crypto.getRandomValues(bytes);
    return Array.from(bytes, function (b) { return b.toString(16).padStart(2, "0"); }).join("");
  }

  function addLog(ok, text) {
    var li = document.createElement("li");
    li.className = ok ? "ok" : "error";
    li.textContent = (ok ? "✓ " : "✗ ") + text;
    log.appendChild(li);
  }

  function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

  // Sends one chunk; retries on network errors and gateway hiccups.
  async function sendChunk(fd) {
    for (var attempt = 1; ; attempt++) {
      try {
        var resp = await fetch(form.dataset.chunkUrl, { method: "POST", body: fd, credentials: "same-origin" });
        if ([502, 503, 504, 520, 522, 524].indexOf(resp.status) >= 0 && attempt < RETRIES) {
          await sleep(1000 * attempt);
          continue;
        }
        var json = null;
        try { json = await resp.json(); } catch (e) { /* not JSON */ }
        if (!json) return { ok: false, message: "server answered " + resp.status };
        return json;
      } catch (e) {
        if (attempt >= RETRIES) return { ok: false, message: "connection failed" };
        await sleep(1000 * attempt);
      }
    }
  }

  async function uploadFile(file, progress) {
    var id = uploadId();
    var offset = 0;
    do {
      var piece = file.slice(offset, offset + chunkSize);
      var fd = baseFields();
      fd.append("upload_id", id);
      fd.append("offset", offset);
      fd.append("last", offset + piece.size >= file.size ? "1" : "0");
      fd.append("filename", file.name);
      fd.append("chunk", piece, file.name);
      var result = await sendChunk(fd);
      if (!result.ok) return { ok: false, message: file.name + ": " + result.message };
      offset += piece.size;
      progress(piece.size);
    } while (offset < file.size);
    return { ok: true, message: result.message || file.name };
  }

  form.addEventListener("submit", async function (e) {
    e.preventDefault();
    var files = Array.from(form.elements.jars.files);
    if (!files.length || busy) return;
    busy = true;
    form.querySelectorAll("button, input, select").forEach(function (el) { el.disabled = true; });
    box.hidden = false;
    log.textContent = "";

    var totalBytes = files.reduce(function (n, f) { return n + f.size; }, 0) || 1;
    var sentBytes = 0, done = 0, uploaded = 0, failed = [];
    function update() {
      bar.style.width = Math.round(100 * sentBytes / totalBytes) + "%";
      status.textContent = "Uploading… " + done + " / " + files.length + " jars";
    }
    update();

    var queue = files.slice();
    async function worker() {
      while (queue.length) {
        var file = queue.shift();
        var result = await uploadFile(file, function (n) { sentBytes += n; update(); });
        done++;
        if (result.ok) { uploaded++; } else { failed.push(result.message); }
        addLog(result.ok, result.message);
        update();
      }
    }
    var workers = [];
    for (var i = 0; i < Math.min(PARALLEL, files.length); i++) workers.push(worker());
    await Promise.all(workers);

    status.textContent = "Updating the pack…";
    var fd = baseFields();
    fd.append("uploaded", uploaded);
    failed.forEach(function (m) { fd.append("failed", m); });
    try {
      await fetch(form.dataset.finishUrl, { method: "POST", body: fd, credentials: "same-origin" });
    } catch (e) { /* the pack page shows the state either way */ }
    busy = false;
    location.replace(location.pathname + location.search);   // back to the same tab
  });
})();
