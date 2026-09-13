import createEngine from "./engine.mjs";
const ready = createEngine();
self.onmessage = async ({ data }) => {
  try {
    const engine = await ready;
    const reply = JSON.parse(engine.execute(JSON.stringify(data.request)));
    self.postMessage({ id: data.id, ...reply });
  } catch {
    self.postMessage({
      id: data.id,
      ok: false,
      error:
        "Calculation engine could not complete this request. Reload and try again.",
    });
  }
};
