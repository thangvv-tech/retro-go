#pragma once

static const char INDEX_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
<title>Retro-Go Wireless Controller</title>
<style>
  :root {
    --chassis-bg: #1e2026;
    --chassis-well: #131417;
    --chassis-border: #2e303a;
    --trigger-bg: linear-gradient(180deg, #373945 0%, #1f2026 100%);
    --dpad-surface: linear-gradient(180deg, #2c2e37 0%, #1a1b21 100%);
    --label-deboss: rgba(255, 255, 255, 0.38);
    --led-off: #3d1414;
    --led-on: #ff3333;
    --led-glow: rgba(255, 51, 51, 0.65);
  }

  * {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    user-select: none;
    -webkit-user-select: none;
    -webkit-touch-callout: none;
    touch-action: none;
    -webkit-tap-highlight-color: transparent;
  }

  html, body {
    width: 100vw;
    height: 100vh;
    overflow: hidden;
    background: #0d0e12;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", monospace, sans-serif;
  }

  /* =========================================================================
     PORTRAIT ROTATION PROMPT OVERLAY
     ========================================================================= */
  .portrait-overlay {
    display: none;
  }

  @media (orientation: portrait) {
    .portrait-overlay {
      display: flex;
      position: fixed;
      inset: 0;
      z-index: 9999;
      background: radial-gradient(circle at center, #1e2027 0%, #0a0b0e 100%);
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 30px 24px;
      text-align: center;
      color: #e0e4f0;
    }

    .controller-layout {
      display: none !important;
    }
  }

  .rotate-card {
    background: #17181f;
    border: 2px solid #2e313d;
    border-radius: 24px;
    padding: 36px 24px;
    box-shadow: 0 16px 36px rgba(0, 0, 0, 0.8), inset 0 1px 1px rgba(255, 255, 255, 0.12);
    display: flex;
    flex-direction: column;
    align-items: center;
    max-width: 340px;
    width: 100%;
  }

  .rotate-icon-anim {
    width: 72px;
    height: 72px;
    margin-bottom: 20px;
    animation: pulse-rotate 2.4s ease-in-out infinite;
  }

  @keyframes pulse-rotate {
    0%, 100% { transform: scale(1) rotate(0deg); }
    50% { transform: scale(1.1) rotate(-90deg); }
  }

  .rotate-badge {
    background: #252834;
    border: 1px solid #3d4254;
    color: #4fc3f7;
    font-size: 10px;
    font-weight: 900;
    letter-spacing: 2px;
    padding: 4px 12px;
    border-radius: 12px;
    margin-bottom: 14px;
  }

  .rotate-title {
    font-size: 17px;
    font-weight: 900;
    letter-spacing: 1.5px;
    color: #ffffff;
    margin-bottom: 10px;
    text-shadow: 0 2px 4px rgba(0, 0, 0, 0.6);
  }

  .rotate-desc {
    font-size: 13px;
    line-height: 1.5;
    color: #8f94a6;
    margin-bottom: 24px;
  }

  .rotate-btn {
    width: 100%;
    padding: 14px 20px;
    background: linear-gradient(180deg, #0288d1 0%, #01579b 100%);
    border: 1px solid #29b6f6;
    border-radius: 14px;
    color: #ffffff;
    font-size: 13px;
    font-weight: 900;
    letter-spacing: 1.5px;
    box-shadow: 0 4px 0 #002f6c, 0 6px 12px rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    cursor: pointer;
    transition: transform 0.05s, box-shadow 0.05s;
  }
  .rotate-btn:active {
    transform: translateY(2px);
    box-shadow: 0 2px 0 #002f6c;
  }

  /* =========================================================================
     LANDSCAPE RETRO CONSOLE GAMEPAD LAYOUT
     ========================================================================= */
  .controller-layout {
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    position: fixed;
    inset: 0;
    width: 100vw;
    height: 100vh;
    padding: env(safe-area-inset-top, 0px) max(14px, env(safe-area-inset-right, 14px)) max(6px, env(safe-area-inset-bottom, 6px)) max(14px, env(safe-area-inset-left, 14px));
    background: radial-gradient(circle at center, #242630 0%, #111215 100%);
    box-shadow: inset 0 0 60px rgba(0, 0, 0, 0.9);
  }

  /* TOP STATUS & SHOULDERS BAR */
  .top-rack {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 2px 4px;
    height: 44px;
    z-index: 20;
  }

  /* ERGONOMIC SHOULDER TRIGGERS (L / R) */
  .trigger {
    width: clamp(96px, 16vw, 140px);
    height: 40px;
    background: var(--trigger-bg);
    border: 2px solid #454756;
    display: flex;
    align-items: center;
    justify-content: center;
    font-weight: 900;
    font-size: 15px;
    letter-spacing: 2px;
    color: #9aa0b4;
    box-shadow: 0 4px 0 #0e0f13, 0 6px 8px rgba(0, 0, 0, 0.6), inset 0 1px 1px rgba(255, 255, 255, 0.2);
    transition: transform 0.05s ease, box-shadow 0.05s ease;
  }
  .trigger-l {
    border-radius: 0 0 16px 4px;
  }
  .trigger-r {
    border-radius: 0 0 4px 16px;
  }
  .trigger:active, .trigger.active {
    transform: translateY(3px);
    box-shadow: 0 1px 0 #0e0f13, inset 0 2px 5px rgba(0, 0, 0, 0.85);
    background: #18191e;
    color: #ffffff;
  }

  /* CENTER SYSTEM EMBLEM & STATUS */
  .brand-panel {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 5px 14px;
    background: #17181e;
    border-radius: 18px;
    border: 1px solid #2d2f3a;
    box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.7), 0 1px 1px rgba(255, 255, 255, 0.08);
  }
  .power-led-wrap {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 9px;
    font-weight: 800;
    letter-spacing: 1px;
    color: #717584;
  }
  .power-led {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--led-off);
    border: 1px solid #200808;
    transition: background 0.2s, box-shadow 0.2s;
  }
  .power-led.connected {
    background: var(--led-on);
    box-shadow: 0 0 8px var(--led-glow), 0 0 2px #fff;
  }
  .logo-text {
    font-size: 11px;
    font-weight: 900;
    letter-spacing: 2px;
    color: #9ea3b5;
    text-shadow: 0 1px 2px #000;
  }
  .player-pill {
    background: #2b2e3b;
    color: #4fc3f7;
    font-size: 9px;
    font-weight: 900;
    padding: 2px 8px;
    border-radius: 10px;
    letter-spacing: 1px;
    border: 1px solid #3c4052;
    cursor: pointer;
    user-select: none;
    transition: transform 0.05s, background 0.1s;
  }
  .player-pill:active {
    background: #0288d1;
    color: #fff;
    transform: scale(0.95);
  }
  .pair-pill {
    background: #3b2b2b;
    color: #ff8a80;
    font-size: 9px;
    font-weight: 900;
    padding: 2px 8px;
    border-radius: 10px;
    letter-spacing: 1px;
    border: 1px solid #5a3c3c;
    cursor: pointer;
    user-select: none;
    transition: transform 0.05s, background 0.1s;
  }
  .pair-pill:active {
    background: #d32f2f;
    color: #fff;
    transform: scale(0.95);
  }
  .fs-btn {
    background: transparent;
    border: none;
    color: #72768a;
    font-size: 14px;
    padding: 2px 6px;
    cursor: pointer;
    line-height: 1;
  }

  /* MAIN PLAYING DECK */
  .main-deck {
    flex: 1;
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0 4px 6px;
    position: relative;
  }

  /* WINGS (LEFT & RIGHT THUMB ZONES) */
  .wing-left, .wing-right {
    display: flex;
    align-items: center;
    justify-content: center;
    flex: 1;
  }

  /* RECESSED WELLS */
  .recessed-well {
    width: clamp(168px, 27vh, 204px);
    height: clamp(168px, 27vh, 204px);
    background: var(--chassis-well);
    border-radius: 50%;
    box-shadow: inset 0 4px 12px rgba(0, 0, 0, 0.85), inset 0 -2px 4px rgba(255, 255, 255, 0.05), 0 1px 1px rgba(255, 255, 255, 0.08);
    display: flex;
    align-items: center;
    justify-content: center;
    position: relative;
  }

  /* RETRO CROSS D-PAD (150px x 150px SYMMETRIC) */
  .dpad-cross {
    width: 150px;
    height: 150px;
    position: relative;
  }
  .dpad-wing {
    position: absolute;
    background: var(--dpad-surface);
    border: 2px solid #3c3e4d;
    box-shadow: 0 4px 0 #101115, 0 6px 8px rgba(0, 0, 0, 0.5), inset 0 1px 1px rgba(255, 255, 255, 0.15);
    display: flex;
    align-items: center;
    justify-content: center;
    transition: transform 0.05s, box-shadow 0.05s;
  }
  .dpad-wing:active, .dpad-wing.active {
    background: #141518;
    box-shadow: 0 1px 0 #101115, inset 0 2px 4px rgba(0, 0, 0, 0.9);
  }
  .dpad-wing-up {
    width: 48px;
    height: 51px;
    top: 0;
    left: 51px;
    border-radius: 8px 8px 0 0;
    border-bottom: none;
  }
  .dpad-wing-down {
    width: 48px;
    height: 51px;
    bottom: 0;
    left: 51px;
    border-radius: 0 0 8px 8px;
    border-top: none;
  }
  .dpad-wing-left {
    width: 51px;
    height: 48px;
    top: 51px;
    left: 0;
    border-radius: 8px 0 0 8px;
    border-right: none;
  }
  .dpad-wing-right {
    width: 51px;
    height: 48px;
    top: 51px;
    right: 0;
    border-radius: 0 8px 8px 0;
    border-left: none;
  }

  /* CENTER PIVOT */
  .dpad-pivot {
    position: absolute;
    width: 48px;
    height: 48px;
    top: 51px;
    left: 51px;
    background: #24252e;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 2;
  }
  .dpad-pivot::after {
    content: '';
    width: 22px;
    height: 22px;
    border-radius: 50%;
    background: #17181e;
    box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.8), 0 1px 1px rgba(255, 255, 255, 0.1);
  }

  /* CRISP VECTOR SVG ARROWS */
  .dpad-arrow {
    width: 15px;
    height: 15px;
    fill: #828799;
    filter: drop-shadow(0 -1px 0 rgba(0, 0, 0, 0.9)) drop-shadow(0 1px 0 rgba(255, 255, 255, 0.12));
    pointer-events: none;
    transition: fill 0.05s;
  }
  .dpad-wing.active .dpad-arrow {
    fill: #ffffff;
    filter: drop-shadow(0 0 4px rgba(255, 255, 255, 0.7));
  }

  /* =========================================================================
     CENTER SYSTEM CONSOLE (SPACED-OUT MENU, OPTION, SELECT, START)
     ========================================================================= */
  .center-console {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 16px;
    padding: 0 16px;
    z-index: 10;
  }

  /* SYSTEM BUTTONS ROW: MENU & OPTION */
  .system-row {
    display: flex;
    gap: 24px;
    align-items: center;
    justify-content: center;
  }
  .btn-sys-pill {
    width: 72px;
    height: 27px;
    border-radius: 14px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 10px;
    font-weight: 900;
    letter-spacing: 1.5px;
    cursor: pointer;
    transition: transform 0.05s, box-shadow 0.05s;
  }
  .btn-menu-color {
    background: linear-gradient(180deg, #d32f2f 0%, #831616 100%);
    border: 1px solid #ef5350;
    color: #ffebee;
    box-shadow: 0 3px 0 #420a0a, 0 4px 6px rgba(0, 0, 0, 0.5);
  }
  .btn-menu-color:active, .btn-menu-color.active {
    transform: translateY(2px);
    box-shadow: 0 1px 0 #420a0a;
    background: #5c0f0f;
  }
  .btn-opt-color {
    background: linear-gradient(180deg, #455a64 0%, #263238 100%);
    border: 1px solid #607d8b;
    color: #eceff1;
    box-shadow: 0 3px 0 #151a1d, 0 4px 6px rgba(0, 0, 0, 0.5);
  }
  .btn-opt-color:active, .btn-opt-color.active {
    transform: translateY(2px);
    box-shadow: 0 1px 0 #151a1d;
    background: #192024;
  }

  /* SPEAKER GRILL */
  .speaker-grille {
    display: grid;
    grid-template-columns: repeat(4, 4px);
    gap: 5px;
    transform: rotate(-25deg);
    opacity: 0.4;
  }
  .speaker-hole {
    width: 4px;
    height: 4px;
    border-radius: 50%;
    background: #0f1013;
    box-shadow: inset 0 1px 1px #000, 0 1px 1px rgba(255, 255, 255, 0.08);
  }

  /* SELECT & START RUBBER PILLS */
  .rubber-pills-row {
    display: flex;
    gap: 34px;
    align-items: center;
    justify-content: center;
  }
  .pill-unit {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
  }
  .btn-rubber-pill {
    width: 48px;
    height: 17px;
    background: linear-gradient(180deg, #3d3f4c 0%, #1f2025 100%);
    border: 1px solid #16171a;
    border-radius: 9px;
    box-shadow: 0 3px 0 #0b0c0e, 0 3px 5px rgba(0, 0, 0, 0.6);
    transform: rotate(-25deg);
    cursor: pointer;
  }
  .btn-rubber-pill:active, .btn-rubber-pill.active {
    transform: rotate(-25deg) translateY(2px);
    box-shadow: 0 1px 0 #0b0c0e;
    background: #121316;
  }
  .pill-label {
    font-size: 9px;
    font-weight: 900;
    letter-spacing: 1.5px;
    color: var(--label-deboss);
    text-shadow: 0 1px 1px #000;
  }

  /* =========================================================================
     SFC-STYLE DIAMOND ACTION CLUSTER
     ========================================================================= */
  .action-deck {
    width: clamp(168px, 27vh, 204px);
    height: clamp(168px, 27vh, 204px);
    background: #17181e;
    border-radius: 40px;
    transform: rotate(-15deg);
    box-shadow: inset 0 3px 10px rgba(0, 0, 0, 0.75), 0 2px 3px rgba(255, 255, 255, 0.06);
    position: relative;
    border: 1px solid #282a34;
  }
  .action-inner {
    width: 100%;
    height: 100%;
    position: relative;
  }

  .btn-orb {
    position: absolute;
    width: 52px;
    height: 52px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 18px;
    font-weight: 900;
    color: #ffffff;
    text-shadow: 0 1px 2px rgba(0, 0, 0, 0.8);
    box-shadow: 0 5px 0 #0e0f13, 0 6px 10px rgba(0, 0, 0, 0.6), inset 0 2px 2px rgba(255, 255, 255, 0.35);
    transition: transform 0.05s, box-shadow 0.05s, filter 0.05s;
  }
  .btn-orb:active, .btn-orb.active {
    transform: translateY(3px);
    box-shadow: 0 2px 0 #0e0f13, inset 0 3px 5px rgba(0, 0, 0, 0.85);
    filter: brightness(1.2);
  }

  /* SFC 4-COLOR THEME */
  .btn-x-orb {
    top: 6px;
    left: 50%;
    transform: translateX(-50%);
    background: radial-gradient(circle at 35% 30%, #29b6f6 0%, #0277bd 70%, #01579b 100%);
    border: 2px solid #4fc3f7;
  }
  .btn-y-orb {
    top: 50%;
    left: 6px;
    transform: translateY(-50%);
    background: radial-gradient(circle at 35% 30%, #66bb6a 0%, #2e7d32 70%, #1b5e20 100%);
    border: 2px solid #81c784;
  }
  .btn-b-orb {
    bottom: 6px;
    left: 50%;
    transform: translateX(-50%);
    background: radial-gradient(circle at 35% 30%, #ffa726 0%, #e65100 70%, #bf360c 100%);
    border: 2px solid #ffb74d;
  }
  .btn-a-orb {
    top: 50%;
    right: 6px;
    transform: translateY(-50%);
    background: radial-gradient(circle at 35% 30%, #ef5350 0%, #c62828 70%, #8e0000 100%);
    border: 2px solid #e57373;
  }

  /* Counter-rotate button labels upright */
  .btn-orb span {
    transform: rotate(15deg);
    display: inline-block;
  }
</style>
</head>
<body>

<!-- PORTRAIT ORIENTATION ROTATE PROMPT -->
<div class="portrait-overlay">
  <div class="rotate-card">
    <div class="rotate-icon-anim">
      <svg width="72" height="72" viewBox="0 0 64 64" fill="none">
        <rect x="10" y="14" width="44" height="36" rx="8" stroke="#4fc3f7" stroke-width="3" fill="#1b1d24"/>
        <circle cx="23" cy="32" r="4.5" fill="#ef5350"/>
        <path d="M41 27v10M36 32h10" stroke="#4fc3f7" stroke-width="2.5" stroke-linecap="round"/>
        <path d="M54 20l5 -4m0 0l-1 5m1 -5c2.5 7 0 16 -6 20" stroke="#ffa726" stroke-width="2.5" stroke-linecap="round"/>
      </svg>
    </div>
    <div class="rotate-badge">RETRO-GO GAMEPAD</div>
    <h2 class="rotate-title">XOAY NGANG ĐIỆN THOẠI</h2>
    <p class="rotate-desc">Để có không gian cầm nắm và điều khiển 2 tay chuẩn công thái học, vui lòng xoay ngang thiết bị.</p>
    <button class="rotate-btn" onclick="requestLandscapeFullscreen()">
      <span>&#x26F6;</span> TOÀN MÀN HÌNH & XOAY
    </button>
  </div>
</div>

<!-- LANDSCAPE CONTROLLER CONTAINER -->
<div class="controller-layout">
  <!-- TOP STATUS & TRIGGER RACK -->
  <div class="top-rack">
    <div class="trigger trigger-l" id="btn_L">L</div>

    <div class="brand-panel">
      <div class="power-led-wrap">
        <div class="power-led" id="powerLed"></div>
        <span id="powerText">DISCONNECTED</span>
      </div>
      <div class="logo-text">RETRO-GO</div>
      <button class="player-pill" id="playerTag" onclick="cyclePlayerId()" title="Bấm để đổi Player 1 / Player 2">PLAYER 1</button>
      <button class="pair-pill" id="pairBtn" onclick="triggerPairing()" title="Ghép đôi lại / Quét kênh">PAIR</button>
      <button class="fs-btn" onclick="toggleFullScreen()" title="Fullscreen">&#x26F6;</button>
    </div>

    <div class="trigger trigger-r" id="btn_R">R</div>
  </div>

  <!-- MAIN PLAYING DECK -->
  <div class="main-deck">
    <!-- LEFT WING: D-PAD DISH -->
    <div class="wing-left">
      <div class="recessed-well" id="dpadWell">
        <div class="dpad-cross" id="dpadCross">
          <div class="dpad-wing dpad-wing-up" id="btn_UP">
            <svg class="dpad-arrow" viewBox="0 0 16 16"><path d="M8 3L2.5 11h11L8 3z"/></svg>
          </div>
          <div class="dpad-wing dpad-wing-down" id="btn_DOWN">
            <svg class="dpad-arrow" viewBox="0 0 16 16"><path d="M8 13L2.5 5h11L8 13z"/></svg>
          </div>
          <div class="dpad-wing dpad-wing-left" id="btn_LEFT">
            <svg class="dpad-arrow" viewBox="0 0 16 16"><path d="M3 8L11 2.5v11L3 8z"/></svg>
          </div>
          <div class="dpad-wing dpad-wing-right" id="btn_RIGHT">
            <svg class="dpad-arrow" viewBox="0 0 16 16"><path d="M13 8L5 2.5v11L13 8z"/></svg>
          </div>
          <div class="dpad-pivot"></div>
        </div>
      </div>
    </div>

    <!-- CENTER CONSOLE -->
    <div class="center-console">
      <!-- SYSTEM BUTTONS: MENU & OPTION -->
      <div class="system-row">
        <div class="btn-sys-pill btn-menu-color" id="btn_MENU">MENU</div>
        <div class="btn-sys-pill btn-opt-color" id="btn_OPTION">OPTION</div>
      </div>

      <!-- ACOUSTIC SPEAKER GRILLE -->
      <div class="speaker-grille">
        <div class="speaker-hole"></div><div class="speaker-hole"></div><div class="speaker-hole"></div><div class="speaker-hole"></div>
        <div class="speaker-hole"></div><div class="speaker-hole"></div><div class="speaker-hole"></div><div class="speaker-hole"></div>
        <div class="speaker-hole"></div><div class="speaker-hole"></div><div class="speaker-hole"></div><div class="speaker-hole"></div>
      </div>

      <!-- CLASSIC RUBBER PILLS: SELECT & START -->
      <div class="rubber-pills-row">
        <div class="pill-unit">
          <div class="btn-rubber-pill" id="btn_SELECT"></div>
          <span class="pill-label">SELECT</span>
        </div>
        <div class="pill-unit">
          <div class="btn-rubber-pill" id="btn_START"></div>
          <span class="pill-label">START</span>
        </div>
      </div>
    </div>

    <!-- RIGHT WING: SFC 4-ACTION DIAMOND -->
    <div class="wing-right">
      <div class="action-deck" id="actionDeck">
        <div class="action-inner">
          <div class="btn-orb btn-x-orb" id="btn_X"><span>X</span></div>
          <div class="btn-orb btn-y-orb" id="btn_Y"><span>Y</span></div>
          <div class="btn-orb btn-b-orb" id="btn_B"><span>B</span></div>
          <div class="btn-orb btn-a-orb" id="btn_A"><span>A</span></div>
        </div>
      </div>
    </div>
  </div>
</div>

<script>
  // Retro-Go key constants matching boards.h
  const KEY = {
    UP:     (1 << 0),
    RIGHT:  (1 << 1),
    DOWN:   (1 << 2),
    LEFT:   (1 << 3),
    SELECT: (1 << 4),
    START:  (1 << 5),
    MENU:   (1 << 6),
    OPTION: (1 << 7),
    A:      (1 << 8),
    B:      (1 << 9),
    X:      (1 << 10),
    Y:      (1 << 11),
    L:      (1 << 12),
    R:      (1 << 13)
  };

  let activeMask = 0;
  let ws = null;
  let currentPlayerId = 0;
  let currentIsPaired = false;

  function cyclePlayerId() {
    const nextPlayer = (currentPlayerId === 0) ? 1 : 0;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(new Uint8Array([0xAA, 0x01, nextPlayer]));
      if (navigator.vibrate) navigator.vibrate(20);
    }
  }

  function triggerPairing() {
    if (confirm('Quét kênh và ghép đôi lại với máy Retro-Go?')) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(new Uint8Array([0xAA, 0x02]));
        if (navigator.vibrate) navigator.vibrate([20, 50, 20]);
      }
    }
  }

  function initWebSocket() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = (location.hostname && location.hostname !== '192.168.4.1') ? '192.168.4.1' : (location.host || '192.168.4.1');
    ws = new WebSocket(`${proto}//${host}/ws`);
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
      document.getElementById('powerLed').classList.add('connected');
      document.getElementById('powerText').textContent = 'ONLINE (ESP-NOW)';
      sendState();
    };

    ws.onmessage = (e) => {
      if (e.data instanceof ArrayBuffer) {
        const d = new Uint8Array(e.data);
        if (d.length >= 5 && d[0] === 0xBB) {
          currentIsPaired = (d[1] === 1);
          currentPlayerId = d[2];
          const ch = d[3];
          const chLocked = (d[4] === 1);

          document.getElementById('playerTag').textContent = 'PLAYER ' + (currentPlayerId + 1);

          const pText = document.getElementById('powerText');
          const pairBtn = document.getElementById('pairBtn');
          if (currentIsPaired && chLocked) {
            pText.textContent = 'CH ' + ch + ' (LOCKED)';
            pText.style.color = '#81c784';
            pairBtn.textContent = 'PAIR';
            pairBtn.style.color = '#81c784';
            pairBtn.style.borderColor = '#2e7d32';
          } else if (!currentIsPaired) {
            pText.textContent = 'PAIRING CH ' + ch;
            pText.style.color = '#ffa726';
            pairBtn.textContent = 'SCANNING...';
            pairBtn.style.color = '#ffa726';
            pairBtn.style.borderColor = '#e65100';
          } else {
            pText.textContent = 'SCAN CH ' + ch;
            pText.style.color = '#ff8a80';
          }
        }
      }
    };

    ws.onclose = () => {
      document.getElementById('powerLed').classList.remove('connected');
      document.getElementById('powerText').textContent = 'DISCONNECTED';
      setTimeout(initWebSocket, 1000);
    };

    ws.onerror = () => {
      ws.close();
    };
  }

  function sendState() {
    if (ws && ws.readyState === WebSocket.OPEN) {
      const buf = new Uint8Array(2);
      buf[0] = activeMask & 0xFF;
      buf[1] = (activeMask >> 8) & 0xFF;
      ws.send(buf);
    }
  }

  function setButton(mask, isDown) {
    const prev = activeMask;
    if (isDown) {
      activeMask |= mask;
    } else {
      activeMask &= ~mask;
    }
    if (activeMask !== prev) {
      if (isDown && navigator.vibrate) {
        navigator.vibrate(12);
      }
      sendState();
    }
  }

  // --- D-PAD TOUCH SLIDING & 8-WAY DIRECTION LOGIC ---
  const dpadWell = document.getElementById('dpadWell');
  const dpadUp = document.getElementById('btn_UP');
  const dpadDown = document.getElementById('btn_DOWN');
  const dpadLeft = document.getElementById('btn_LEFT');
  const dpadRight = document.getElementById('btn_RIGHT');

  function updateDpadFromCoords(clientX, clientY) {
    const rect = dpadWell.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const dx = clientX - cx;
    const dy = clientY - cy;
    const dist = Math.hypot(dx, dy);

    let up = false, down = false, left = false, right = false;

    // 12px deadzone
    if (dist > 12) {
      if (dy < -12) up = true;
      if (dy > 12)  down = true;
      if (dx < -12) left = true;
      if (dx > 12)  right = true;
    }

    dpadUp.classList.toggle('active', up);
    dpadDown.classList.toggle('active', down);
    dpadLeft.classList.toggle('active', left);
    dpadRight.classList.toggle('active', right);

    let dpadMask = 0;
    if (up)    dpadMask |= KEY.UP;
    if (down)  dpadMask |= KEY.DOWN;
    if (left)  dpadMask |= KEY.LEFT;
    if (right) dpadMask |= KEY.RIGHT;

    // Preserve non-D-Pad keys
    const nonDpad = activeMask & ~(KEY.UP | KEY.DOWN | KEY.LEFT | KEY.RIGHT);
    const newMask = nonDpad | dpadMask;
    if (newMask !== activeMask) {
      if (dpadMask !== 0 && (activeMask & (KEY.UP | KEY.DOWN | KEY.LEFT | KEY.RIGHT)) === 0 && navigator.vibrate) {
        navigator.vibrate(12);
      }
      activeMask = newMask;
      sendState();
    }
  }

  function clearDpad() {
    dpadUp.classList.remove('active');
    dpadDown.classList.remove('active');
    dpadLeft.classList.remove('active');
    dpadRight.classList.remove('active');
    const nonDpad = activeMask & ~(KEY.UP | KEY.DOWN | KEY.LEFT | KEY.RIGHT);
    if (nonDpad !== activeMask) {
      activeMask = nonDpad;
      sendState();
    }
  }

  dpadWell.addEventListener('touchstart', (e) => {
    e.preventDefault();
    if (e.targetTouches.length > 0) {
      updateDpadFromCoords(e.targetTouches[0].clientX, e.targetTouches[0].clientY);
    }
  }, {passive: false});

  dpadWell.addEventListener('touchmove', (e) => {
    e.preventDefault();
    if (e.targetTouches.length > 0) {
      updateDpadFromCoords(e.targetTouches[0].clientX, e.targetTouches[0].clientY);
    }
  }, {passive: false});

  dpadWell.addEventListener('touchend', (e) => {
    e.preventDefault();
    clearDpad();
  }, {passive: false});

  dpadWell.addEventListener('touchcancel', (e) => {
    e.preventDefault();
    clearDpad();
  }, {passive: false});

  // Desktop mouse support for D-Pad
  let dpadMouseDown = false;
  dpadWell.addEventListener('mousedown', (e) => {
    e.preventDefault();
    dpadMouseDown = true;
    updateDpadFromCoords(e.clientX, e.clientY);
  });
  window.addEventListener('mousemove', (e) => {
    if (dpadMouseDown) {
      updateDpadFromCoords(e.clientX, e.clientY);
    }
  });
  window.addEventListener('mouseup', () => {
    if (dpadMouseDown) {
      dpadMouseDown = false;
      clearDpad();
    }
  });

  // --- DISCRETE BUTTON BINDINGS ---
  function bindButton(id, mask) {
    const el = document.getElementById(id);
    if (!el) return;

    function down(e) {
      e.preventDefault();
      el.classList.add('active');
      setButton(mask, true);
    }

    function up(e) {
      e.preventDefault();
      el.classList.remove('active');
      setButton(mask, false);
    }

    el.addEventListener('touchstart', down, {passive: false});
    el.addEventListener('touchend', up, {passive: false});
    el.addEventListener('touchcancel', up, {passive: false});
    el.addEventListener('mousedown', down);
    el.addEventListener('mouseup', up);
    el.addEventListener('mouseleave', up);
  }

  bindButton('btn_A', KEY.A);
  bindButton('btn_B', KEY.B);
  bindButton('btn_X', KEY.X);
  bindButton('btn_Y', KEY.Y);
  bindButton('btn_L', KEY.L);
  bindButton('btn_R', KEY.R);
  bindButton('btn_SELECT', KEY.SELECT);
  bindButton('btn_START', KEY.START);
  bindButton('btn_MENU', KEY.MENU);
  bindButton('btn_OPTION', KEY.OPTION);

  // --- FULLSCREEN & ORIENTATION CONTROLS ---
  function toggleFullScreen() {
    if (!document.fullscreenElement) {
      requestLandscapeFullscreen();
    } else {
      document.exitFullscreen().catch(() => {});
    }
  }

  function requestLandscapeFullscreen() {
    const el = document.documentElement;
    const reqFs = el.requestFullscreen || el.webkitRequestFullscreen || el.mozRequestFullScreen || el.msRequestFullscreen;
    if (reqFs) {
      reqFs.call(el).then(() => {
        if (screen.orientation && screen.orientation.lock) {
          screen.orientation.lock('landscape').catch(() => {});
        }
      }).catch(() => {});
    } else if (screen.orientation && screen.orientation.lock) {
      screen.orientation.lock('landscape').catch(() => {});
    }
  }

  // Periodic heartbeat keeps connection alive
  setInterval(() => {
    if (ws && ws.readyState === WebSocket.OPEN) {
      sendState();
    }
  }, 10000);

  window.addEventListener('load', initWebSocket);
</script>
</body>
</html>
)rawliteral";
