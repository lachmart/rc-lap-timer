// RC Lap Timer – beep, voice and vibration (opt-in, remembered per phone). Part of the client, see app.js.

let beepOn = false, voiceOn = false, ac = null;

function setToggle(btn, on, label) {
  btn.textContent = label;
  btn.classList.toggle('on', on);
  btn.setAttribute('aria-pressed', on);
}

// Remembered per phone. Browser storage can be unavailable (private mode), then it just isn't kept.
const prefs = {
  get: k => { try { return localStorage.getItem(k) === '1'; } catch (e) { return false; } },
  set: (k, v) => { try { localStorage.setItem(k, v ? '1' : '0'); } catch (e) { /* not kept */ } },
};

function unlockAudio() {
  try {
    if (!ac) ac = new (window.AudioContext || window.webkitAudioContext)();
    if (ac.state === 'suspended') ac.resume();
  } catch (e) { /* no WebAudio */ }
}

function setBeep(on) {
  beepOn = on;
  setToggle($('snd'), on, 'Beep');
  prefs.set('beep', on);
  if (on) unlockAudio();
}

function setVoice(on, greet) {
  if (!('speechSynthesis' in window)) { $('voice').textContent = 'No voice'; return; }
  voiceOn = on;
  setToggle($('voice'), on, 'Voice');
  prefs.set('voice', on);
  if (on && greet) {
    speechSynthesis.cancel();
    say('Voice on', 'now');
  }
}

function tone(freq, secs) {
  if (!beepOn || !ac) return;
  const o = ac.createOscillator(), g = ac.createGain();
  o.frequency.value = freq;
  g.gain.value = .2;
  o.connect(g); g.connect(ac.destination);
  o.start(); o.stop(ac.currentTime + secs);
}

function beep(best) {
  if (navigator.vibrate) navigator.vibrate(best ? [80, 60, 80] : 80);
  tone(best ? 1320 : 880, best ? .35 : .15);
}

// A new announcement interrupts the previous one; mode 'queue' waits for it instead (several
// messages at once, e.g. a heat result and the next driver). Phone browsers (iOS Safari above all)
// drop an utterance spoken in the same moment as cancel(), so cancel only when something is being
// said and speak a moment later; 'now' speaks at once, inside the tap (needed to unlock speech).
let delayed = null;     // utterances waiting out the moment after cancel(), in order

function say(text, mode = '') {
  if (!voiceOn) return;
  const u = new SpeechSynthesisUtterance(text);
  u.lang = 'en-US';
  if (mode === 'now') { speechSynthesis.speak(u); return; }
  if (delayed) {                                       // an interruption is under way: join it
    if (mode === 'queue') delayed.push(u); else delayed.splice(0, delayed.length, u);
    return;
  }
  if (mode === 'queue' || !(speechSynthesis.speaking || speechSynthesis.pending)) { speechSynthesis.speak(u); return; }
  speechSynthesis.cancel();
  delayed = [u];
  setTimeout(() => { const list = delayed; delayed = null; for (const x of list) speechSynthesis.speak(x); }, 60);
}

function announce(lap, kind) {
  const name = multi() || S.drivers.length ? whoOn(lap[L.CAR]) : '';
  let text = (name ? name + ', ' : '') + (lap[L.MS] / 1000).toFixed(2);
  if (kind === 'best') text += ', best lap';
  else if (kind === 'pb') text += ', personal best';
  say(text);
}

// Browsers only allow sound after the user touched the page: restore the saved switches
// and unlock audio and speech on the first tap anywhere.
function soundInit() {
  $('snd').onclick   = () => setBeep(!beepOn);
  $('voice').onclick = () => setVoice(!voiceOn, true);
  setBeep(prefs.get('beep'));
  setVoice(prefs.get('voice'), false);
  document.addEventListener('pointerdown', e => {
    if (beepOn) unlockAudio();
    // unlock speech with a silent utterance, unless this tap is the switch itself (it speaks)
    if (voiceOn && e.target !== $('voice')) speechSynthesis.speak(new SpeechSynthesisUtterance(' '));
  }, { once: true });
}
