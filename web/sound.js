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
  if (!on) { speechSynthesis.cancel(); talking.clear(); setVoiceStuck(false); }
  if (on && greet) say('Voice on');                    // inside the tap: unlocks speech on phones
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

// Speech: messages are short and queue up, each is said in full; nothing is ever cut off.
// Utterances are kept until they end (Chrome stalls if one is garbage-collected early).
const talking = new Set();
let heardAt = 0;       // last time speech started or ended

function say(text) {
  if (!voiceOn) return;
  const u = new SpeechSynthesisUtterance(text);
  u.lang = 'en-US';
  if (!talking.size) heardAt = Date.now();
  talking.add(u);
  u.onstart = () => { heardAt = Date.now(); setVoiceStuck(false); };
  u.onend = u.onerror = () => { heardAt = Date.now(); talking.delete(u); };
  speechSynthesis.resume();                            // Chrome can sit paused after the tab was hidden
  speechSynthesis.speak(u);
  // Nothing started or ended for a while although messages wait: the browser's speech is stuck
  // (seen in Chrome on macOS). Say so on the switch.
  setTimeout(() => { if (voiceOn && talking.size && Date.now() - heardAt >= 8000) setVoiceStuck(true); }, 8100);
}

function setVoiceStuck(stuck) {
  $('voice').classList.toggle('stuck', stuck);
  $('voice').title = stuck ? 'This browser accepts speech but doesn\'t play it. Restart the browser (Chrome on a Mac: quit with ⌘Q).'
    : 'Read out lap times';
}

// A lap: its time, "best" for the best lap of the session; with several drivers or cars who it was.
function lapText(lap, kind, withName) {
  const name = withName ? whoOn(lap[L.CAR]) : '';
  return (name ? name + ', ' : '') + (lap[L.MS] / 1000).toFixed(2) + (kind === 'best' ? ', best' : '');
}

function announce(lap, kind) {
  say(lapText(lap, kind, multi() || S.drivers.length > 0));
}

// Browsers only allow sound after the user touched the page: restore the saved switches
// and unlock audio and speech on the first tap anywhere.
function soundInit() {
  $('snd').onclick   = () => setBeep(!beepOn);
  $('voice').onclick = () => setVoice(!voiceOn, true);
  setBeep(prefs.get('beep'));
  setVoice(prefs.get('voice'), false);
  document.addEventListener('pointerdown', () => {
    if (beepOn) unlockAudio();
    if (voiceOn) speechSynthesis.speak(new SpeechSynthesisUtterance(''));   // unlocks speech, says nothing
  }, { once: true });
}
