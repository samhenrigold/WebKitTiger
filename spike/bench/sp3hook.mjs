// Loaded after Speedometer's own main.mjs (fetch.sh adds the tag): reports progress and the
// result through document.title and the console, which TigerBrowser2 logs as "TIGER title:"
// (and TIGER-CONSOLE with TIGER_CONSOLE=1), so a run on the box needs nobody to read the screen.
const c = globalThis.benchmarkClient;
const t0 = performance.now();
const say = (s) => { document.title = s; console.log(s); };
const didRunTest = c.didRunTest.bind(c);
c.didRunTest = function () {
    didRunTest();
    say(`SP3 progress ${this._finishedTestCount}/${this.stepCount} ${document.getElementById("info-label").textContent} at ${((performance.now() - t0) / 1000).toFixed(1)}s`);
};
const done = c.didFinishLastIteration.bind(c);
c.didFinishLastIteration = function (metrics) {
    done(metrics);
    for (const [name, m] of Object.entries(metrics))
        if (!name.includes("/")) console.log(`SP3 metric ${name} mean=${m.mean?.toFixed(2)} ${m.unit}`);
    // After the section switch, which sets the title itself.
    const score = `SP3 score ${document.getElementById("result-number").textContent} after ${((performance.now() - t0) / 1000).toFixed(1)}s`;
    setTimeout(() => say(score), 0);
};
