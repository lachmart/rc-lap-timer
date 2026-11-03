// RC Lap Timer – beep, voice and vibration (opt-in, remembered per phone). Part of the client, see app.js.

let beepOn = false, voiceOn = false, ac = null;

function setToggle(btn, on, label) { btn.textContent = (on ? '🔊 ' : '🔇 ') + label; }

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
  if (on && greet) say('Voice on');
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

function say(text) {
  if (!voiceOn) return;
  speechSynthesis.cancel();                            // a new lap interrupts the previous announcement
  const u = new SpeechSynthesisUtterance(text);
  u.lang = 'en-US';
  speechSynthesis.speak(u);
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
  document.addEventListener('pointerdown', () => {
    if (beepOn) unlockAudio();
    if (voiceOn) speechSynthesis.speak(new SpeechSynthesisUtterance(''));
  }, { once: true });
}
