/* The page owns only transport, controls, and presentation. All guest state
 * and emulation lives behind the small C API in emu_api.c. */
(() => {
  "use strict";

  const canvas = document.getElementById("screen");
  const context = canvas.getContext("2d");
  const status = document.getElementById("status");
  const frame = context.createImageData(320, 480);
  const serial = new Uint8Array(8192);
  let module;
  let run;
  let frameCall;
  let cyclesCall;
  let serialRead;
  let framePointer;
  let widthPointer;
  let heightPointer;
  let serialPointer;
  let startedAt;
  let totalSteps = 0;
  let pointerDown = false;
  let downloadReceived = 0;
  let downloadTotal = 0;
  const queryCpi = Number.parseInt(new URLSearchParams(location.search).get("cpi"), 10);
  const cpi = Number.isInteger(queryCpi) && queryCpi >= 1 && queryCpi <= 16 ? queryCpi : 1;

  const setStatus = text => { status.textContent = text; };
  const fail = (message, error) => {
    setStatus(`${message}${error ? `\n${error}` : ""}`);
    console.error(message, error || "");
  };

  function downloadStatus(label) {
    const total = downloadTotal ? ` / ${downloadTotal.toLocaleString()} bytes` : "";
    setStatus(`Downloading ${label}: ${downloadReceived.toLocaleString()} bytes${total}`);
  }

  async function bytes(parts, label) {
    if (!parts) return null;
    const names = Array.isArray(parts) ? parts : [parts];
    try {
      const responses = await Promise.all(names.map(name => fetch(name)));
      for (let i = 0; i < responses.length; i++) {
        if (!responses[i].ok)
          throw new Error(`${label}: ${names[i]} returned HTTP ${responses[i].status}`);
        const length = Number(responses[i].headers.get("Content-Length"));
        if (Number.isFinite(length)) downloadTotal += length;
      }
      const chunks = [];
      for (const response of responses) {
        if (!response.body) {
          const chunk = new Uint8Array(await response.arrayBuffer());
          chunks.push(chunk);
          downloadReceived += chunk.length;
          downloadStatus(label);
          continue;
        }
        const reader = response.body.getReader();
        const received = [];
        for (;;) {
          const part = await reader.read();
          if (part.done) break;
          received.push(part.value);
          downloadReceived += part.value.length;
          downloadStatus(label);
        }
        chunks.push(received);
      }
      const total = chunks.reduce((n, chunk) =>
        n + (Array.isArray(chunk) ? chunk.reduce((m, part) => m + part.length, 0) : chunk.length), 0);
      const output = new Uint8Array(total);
      let offset = 0;
      for (const chunk of chunks) {
        if (Array.isArray(chunk)) {
          for (const part of chunk) {
            output.set(part, offset);
            offset += part.length;
          }
        } else {
          output.set(chunk, offset);
          offset += chunk.length;
        }
      }
      const first = names[0];
      if (first.endsWith(".gz") || /\.gz\.part\d+$/.test(first)) {
        if (typeof DecompressionStream === "undefined")
          throw new Error("gzip assets require browser DecompressionStream support");
        const stream = new Blob([output]).stream().pipeThrough(
          new DecompressionStream("gzip"));
        return new Uint8Array(await new Response(stream).arrayBuffer());
      }
      return output;
    } catch (error) {
      throw new Error(`${label} could not be loaded: ${error.message}`);
    }
  }

  function putBytes(data) {
    const pointer = module._malloc(data.length);
    if (!pointer) throw new Error("WASM allocation failed");
    module.HEAPU8.set(data, pointer);
    return [pointer, data.length];
  }

  function callButton(which, pressed) {
    if (!run) return;
    module.ccall("emu_button", "number", ["number", "number"], [which, pressed ? 1 : 0]);
  }

  function releaseButton(which) {
    callButton(which, false);
  }

  function pressButton(which) {
    callButton(which, true);
    window.setTimeout(() => releaseButton(which), 100);
  }

  function keyButton(event) {
    const key = event.key.toLowerCase();
    if (key === "escape" || key === "h") return 1;
    if (key === "p") return 0;
    if (event.key === "+" || event.key === "=") return 2;
    if (event.key === "-" || event.key === "_") return 3;
    return -1;
  }

  function point(event) {
    const rect = canvas.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(319, Math.floor((event.clientX - rect.left) * 320 / rect.width))),
      y: Math.max(0, Math.min(479, Math.floor((event.clientY - rect.top) * 480 / rect.height)))
    };
  }

  canvas.addEventListener("pointerdown", event => {
    if (!run) return;
    pointerDown = true;
    canvas.setPointerCapture(event.pointerId);
    const p = point(event);
    module.ccall("emu_touch", "number", ["number", "number", "number"], [1, p.x, p.y]);
    event.preventDefault();
  });
  canvas.addEventListener("pointermove", event => {
    if (!run || !pointerDown) return;
    const p = point(event);
    module.ccall("emu_touch", "number", ["number", "number", "number"], [2, p.x, p.y]);
    event.preventDefault();
  });
  canvas.addEventListener("pointerup", event => {
    if (!run) return;
    pointerDown = false;
    module.ccall("emu_touch", "number", ["number", "number", "number"], [0, 0, 0]);
    event.preventDefault();
  });
  canvas.addEventListener("pointercancel", () => {
    pointerDown = false;
    if (run) module.ccall("emu_touch", "number", ["number", "number", "number"], [0, 0, 0]);
  });
  document.querySelectorAll("[data-button]").forEach(button => {
    const which = Number(button.dataset.button);
    button.addEventListener("click", () => pressButton(which));
  });
  document.addEventListener("keydown", event => {
    if (event.repeat) return;
    const which = keyButton(event);
    if (which < 0) return;
    callButton(which, true);
    event.preventDefault();
  });
  document.addEventListener("keyup", event => {
    const which = keyButton(event);
    if (which < 0) return;
    callButton(which, false);
    event.preventDefault();
  });

  function animationFrame() {
    if (!run) return;
    const began = performance.now();
    let steps = 0;
    while (performance.now() - began < 12) {
      const n = run(1000000);
      steps += n;
      if (!n) break;
    }
    totalSteps += steps;
    const result = frameCall(framePointer, widthPointer, heightPointer);
    let display = "scanout unavailable";
    if (result > 0) {
      frame.data.set(module.HEAPU8.subarray(framePointer, framePointer + frame.data.length));
      context.putImageData(frame, 0, 0);
      display = `scanout ${module.HEAPU32[widthPointer >> 2]}×${module.HEAPU32[heightPointer >> 2]}`;
    }
    const elapsed = Math.max(.001, (performance.now() - startedAt) / 1000);
    const speed = Math.round(totalSteps / elapsed).toLocaleString();
    let suffix = "";
    const n = serialRead(serialPointer, serial.byteLength);
    if (n) {
      serial.set(module.HEAPU8.subarray(serialPointer, serialPointer + n));
      const text = new TextDecoder().decode(serial.subarray(0, n));
      suffix = `\nUART0: ${text.slice(-180)}`;
    }
    setStatus(`Running · ${speed} instructions/s · cpi ${cpi} · cycle ${cyclesCall()} · ${display}${suffix}`);
    requestAnimationFrame(animationFrame);
  }

  async function start() {
    try {
      const manifestResponse = await fetch("manifest.json");
      if (!manifestResponse.ok)
        throw new Error(`manifest.json returned HTTP ${manifestResponse.status}`);
      const manifest = await manifestResponse.json();
      const hasSnapshot = Array.isArray(manifest.snapshot) && manifest.snapshot.length > 0;
      if (!hasSnapshot && (!manifest.rootfs ||
                          (Array.isArray(manifest.rootfs) && !manifest.rootfs.length)))
        throw new Error("manifest has neither a snapshot nor a rootfs for cold boot");
      if (!hasSnapshot)
        setStatus("Cold boot selected; loading rootfs (this takes several minutes)…");
      const [kernel, devicetree, rootfs, snapshot] = await Promise.all([
        bytes(manifest.kernel, "kernel"),
        bytes(manifest.devicetree, "device tree"),
        hasSnapshot ? Promise.resolve(null) : bytes(manifest.rootfs, "rootfs"),
        hasSnapshot ? bytes(manifest.snapshot, "snapshot") : Promise.resolve(null)
      ]);
      setStatus("Initializing the M68 machine…");
      module = await createEmu({locateFile: path => `dist/${path}`});
      const k = putBytes(kernel), d = putBytes(devicetree);
      const r = rootfs ? putBytes(rootfs) : [0, 0];
      const init = module.ccall("emu_init", "number",
        ["number", "number", "number", "number", "number", "number", "number"],
        [k[0], k[1], d[0], d[1], r[0], r[1], 256]);
      module._free(k[0]); module._free(d[0]); if (r[0]) module._free(r[0]);
      if (init < 0) throw new Error(module.ccall("emu_error", "string", [], []));
      if (snapshot) {
        setStatus("Loading snapshot…");
        const s = putBytes(snapshot);
        const loaded = module.ccall("emu_load_snapshot", "number", ["number", "number"], s);
        module._free(s[0]);
        if (loaded < 0) throw new Error(module.ccall("emu_error", "string", [], []));
      }
      const cpiResult = module.ccall("emu_set_cpi", "number", ["number"], [cpi]);
      if (cpiResult < 0) throw new Error(`invalid cpi ${cpi}`);
      run = module.cwrap("emu_run", "number", ["number"]);
      frameCall = module.cwrap("emu_frame", "number", ["number", "number", "number"]);
      cyclesCall = module.cwrap("emu_cycles", "number", []);
      serialRead = module.cwrap("emu_serial_read", "number", ["number", "number"]);
      framePointer = module._malloc(frame.data.length);
      widthPointer = module._malloc(4);
      heightPointer = module._malloc(4);
      serialPointer = module._malloc(serial.byteLength);
      if (!framePointer || !widthPointer || !heightPointer || !serialPointer)
        throw new Error("WASM frontend buffer allocation failed");
      startedAt = performance.now();
      requestAnimationFrame(animationFrame);
    } catch (error) {
      fail("Emulator startup failed.", error.message);
    }
  }

  start();
})();
